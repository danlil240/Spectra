//! Topic publishing: stream samples to a named topic that any Spectra
//! window (or [`crate::Axes::subscribe_topic`]) can plot live.

use std::time::{Duration, Instant};

use crate::codec;
use crate::error::{Error, Result};
use crate::launcher;
use crate::protocol as p;
use crate::session::handshake;
use crate::transport::{Polled, Transport};

/// Minimum time between reconnect attempts after the broker went away.
const RECONNECT_BACKOFF: Duration = Duration::from_millis(500);
/// How long to wait for the broker to acknowledge a request.
const REPLY_TIMEOUT: Duration = Duration::from_secs(5);
const LAUNCH_TIMEOUT: Duration = Duration::from_secs(10);

/// Shape of each sample on a topic.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TopicKind {
    /// `(x, y)` pairs.
    #[default]
    Scalar2D,
    /// `(x, y, z)` triples.
    Scalar3D,
}

impl TopicKind {
    fn wire(self) -> u8 {
        match self {
            TopicKind::Scalar2D => 0,
            TopicKind::Scalar3D => 1,
        }
    }

    /// Values per sample.
    pub fn stride(self) -> usize {
        match self {
            TopicKind::Scalar2D => 2,
            TopicKind::Scalar3D => 3,
        }
    }
}

/// Options for [`Publisher::with_options`].
#[derive(Debug, Clone)]
pub struct PublisherOptions {
    /// Sample shape.
    pub kind: TopicKind,
    /// Unit shown next to the topic (e.g. `"m/s^2"`).
    pub unit: String,
    /// Broker-side ring buffer capacity in samples.
    pub ring_capacity: u32,
    /// Broker socket. `None` follows `$SPECTRA_SOCKET` / the newest broker,
    /// and is re-resolved on every reconnect.
    pub socket: Option<String>,
    /// Start `spectra-backend` if no broker is running.
    pub auto_launch: bool,
}

impl Default for PublisherOptions {
    fn default() -> Self {
        PublisherOptions {
            kind: TopicKind::Scalar2D,
            unit: String::new(),
            ring_capacity: 4096,
            socket: None,
            auto_launch: true,
        }
    }
}

/// Streams samples to a named topic on the Spectra broker.
///
/// The first connection must succeed. After that, if the broker goes away
/// (the app was closed), [`Publisher::publish`] drops samples and retries the
/// connection at most twice a second, so publish loops keep running across
/// app restarts.
///
/// ```no_run
/// use spectra_plot::Publisher;
///
/// let mut imu = Publisher::new("sensor/imu/accel_x")?;
/// for i in 0..1000 {
///     let t = i as f64 * 0.01;
///     imu.publish(t, t.sin())?;
/// }
/// # Ok::<(), spectra_plot::Error>(())
/// ```
pub struct Publisher {
    name: String,
    opts: PublisherOptions,
    transport: Option<Transport>,
    session_id: u64,
    next_request_id: u64,
    socket_path: String,
    next_reconnect_at: Instant,
    closed: bool,
}

impl std::fmt::Debug for Publisher {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Publisher")
            .field("name", &self.name)
            .field("kind", &self.opts.kind)
            .field("socket", &self.socket_path)
            .field("connected", &self.transport.is_some())
            .finish()
    }
}

impl Publisher {
    /// Declare a 2D topic, connecting to (or launching) the broker.
    pub fn new(name: &str) -> Result<Publisher> {
        Publisher::with_options(name, PublisherOptions::default())
    }

    /// Declare a topic with explicit options.
    pub fn with_options(name: &str, opts: PublisherOptions) -> Result<Publisher> {
        if name.is_empty() {
            return Err(Error::invalid("topic name must be non-empty"));
        }
        let mut pubr = Publisher {
            name: name.to_string(),
            opts,
            transport: None,
            session_id: 0,
            next_request_id: 0,
            socket_path: String::new(),
            next_reconnect_at: Instant::now(),
            closed: false,
        };
        pubr.connect_and_declare()?;
        Ok(pubr)
    }

    /// Topic name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Sample shape.
    pub fn kind(&self) -> TopicKind {
        self.opts.kind
    }

    /// Socket of the broker last connected to.
    pub fn socket_path(&self) -> &str {
        &self.socket_path
    }

    /// Whether the broker connection is up.
    pub fn is_connected(&mut self) -> bool {
        self.transport.as_mut().is_some_and(|t| t.is_alive())
    }

    /// Publish one `(x, y)` sample to a 2D topic.
    pub fn publish(&mut self, x: f64, y: f64) -> Result<()> {
        self.check_kind(TopicKind::Scalar2D)?;
        self.publish_interleaved(&[x, y])
    }

    /// Publish one `(x, y, z)` sample to a 3D topic.
    pub fn publish3(&mut self, x: f64, y: f64, z: f64) -> Result<()> {
        self.check_kind(TopicKind::Scalar3D)?;
        self.publish_interleaved(&[x, y, z])
    }

    /// Publish many samples at once, interleaved (`x0, y0, x1, y1, ...` for
    /// 2D topics, `x0, y0, z0, ...` for 3D topics).
    pub fn publish_interleaved(&mut self, samples: &[f64]) -> Result<()> {
        if self.closed {
            return Err(Error::connection("publisher is closed"));
        }
        if samples.is_empty() {
            return Ok(());
        }
        let stride = self.opts.kind.stride();
        if samples.len() % stride != 0 {
            return Err(Error::invalid(format!(
                "{} values is not a multiple of the topic stride {stride}",
                samples.len()
            )));
        }
        let payload = codec::encode_publish_topic_samples(&self.name, samples);
        if !self.is_connected() {
            self.drop_transport();
        }
        if self.transport.is_some() && self.request(p::REQ_PUBLISH_TOPIC_SAMPLES, &payload).is_ok()
        {
            return Ok(());
        }
        // The broker went away: retry (rate-limited), otherwise drop the samples.
        if !self.maybe_reconnect() {
            return Ok(());
        }
        if self
            .request(p::REQ_PUBLISH_TOPIC_SAMPLES, &payload)
            .is_err()
        {
            self.drop_transport();
        }
        Ok(())
    }

    /// Publish parallel `x` / `y` slices.
    pub fn publish_batch(&mut self, x: &[f64], y: &[f64]) -> Result<()> {
        self.check_kind(TopicKind::Scalar2D)?;
        if x.len() != y.len() {
            return Err(Error::invalid("x and y must have the same length"));
        }
        let flat: Vec<f64> = x.iter().zip(y).flat_map(|(a, b)| [*a, *b]).collect();
        self.publish_interleaved(&flat)
    }

    /// Publish parallel `x` / `y` / `z` slices.
    pub fn publish_batch3(&mut self, x: &[f64], y: &[f64], z: &[f64]) -> Result<()> {
        self.check_kind(TopicKind::Scalar3D)?;
        if x.len() != y.len() || x.len() != z.len() {
            return Err(Error::invalid("x, y and z must have the same length"));
        }
        let flat: Vec<f64> = x
            .iter()
            .zip(y)
            .zip(z)
            .flat_map(|((a, b), c)| [*a, *b, *c])
            .collect();
        self.publish_interleaved(&flat)
    }

    /// Disconnect. The topic disappears from the broker.
    pub fn close(&mut self) {
        self.closed = true;
        self.drop_transport();
    }

    fn check_kind(&self, want: TopicKind) -> Result<()> {
        if self.opts.kind == want {
            Ok(())
        } else {
            Err(Error::invalid(format!(
                "topic '{}' is {:?}, not {:?}",
                self.name, self.opts.kind, want
            )))
        }
    }

    fn drop_transport(&mut self) {
        self.transport = None;
        self.session_id = 0;
    }

    fn maybe_reconnect(&mut self) -> bool {
        if self.closed {
            return false;
        }
        let now = Instant::now();
        if now < self.next_reconnect_at {
            return false;
        }
        self.next_reconnect_at = now + RECONNECT_BACKOFF;
        self.connect_and_declare().is_ok()
    }

    fn connect_and_declare(&mut self) -> Result<()> {
        self.drop_transport();
        let explicit = self.opts.socket.as_deref();
        let mut candidates = launcher::socket_candidates(explicit);
        if self.opts.auto_launch {
            let pinned = explicit.is_some() || std::env::var_os("SPECTRA_SOCKET").is_some();
            if pinned || candidates.is_empty() {
                let path = candidates
                    .first()
                    .cloned()
                    .unwrap_or_else(|| launcher::resolve_socket_path(explicit));
                candidates = vec![launcher::ensure_backend(&path, LAUNCH_TIMEOUT)?];
            }
        }
        let mut last = Error::connection(format!(
            "no Spectra broker found to publish topic '{}'",
            self.name
        ));
        for path in candidates.into_iter().filter(|p| !p.is_empty()) {
            match self.connect_path(&path) {
                Ok(()) => return Ok(()),
                Err(e) => last = e,
            }
        }
        Err(last)
    }

    fn connect_path(&mut self, path: &str) -> Result<()> {
        let (transport, sid) = handshake(path, "publisher")?;
        self.transport = Some(transport);
        self.session_id = sid;
        self.next_request_id = 0;
        self.socket_path = path.to_string();
        let declare = codec::encode_declare_topic(
            &self.name,
            self.opts.kind.wire(),
            &self.opts.unit,
            self.opts.ring_capacity,
        );
        if let Err(e) = self.request(p::REQ_DECLARE_TOPIC, &declare) {
            self.drop_transport();
            return Err(e);
        }
        Ok(())
    }

    fn request(&mut self, msg_type: u16, payload: &[u8]) -> Result<()> {
        let Some(t) = self.transport.as_mut() else {
            return Err(Error::connection("publisher is not connected"));
        };
        self.next_request_id += 1;
        let req_id = self.next_request_id;
        t.send(msg_type, payload, req_id, self.session_id)?;
        let deadline = Instant::now() + REPLY_TIMEOUT;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Err(Error::Timeout(format!(
                    "broker did not answer {}",
                    p::message_name(msg_type)
                )));
            }
            let msg = match t.poll(left)? {
                Polled::Message(m) => m,
                Polled::Nothing => continue,
                Polled::Closed => return Err(Error::connection("broker closed the connection")),
            };
            let h = msg.header;
            if h.msg_type == p::RESP_ERR {
                let (rid, code, message) = codec::decode_resp_err(&msg.payload);
                if rid == req_id || rid == 0 || h.request_id == req_id {
                    return Err(Error::Backend { code, message });
                }
            }
            if h.request_id == req_id
                || (h.msg_type == p::RESP_OK && codec::decode_resp_ok(&msg.payload) == req_id)
            {
                return Ok(());
            }
        }
    }
}

impl Drop for Publisher {
    fn drop(&mut self) {
        self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strides() {
        assert_eq!(TopicKind::Scalar2D.stride(), 2);
        assert_eq!(TopicKind::Scalar3D.stride(), 3);
        assert_eq!(TopicKind::Scalar3D.wire(), 1);
    }
}
