//! The connection to the Spectra backend.

use std::collections::{HashMap, VecDeque};
use std::env;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread;
use std::time::{Duration, Instant};

use crate::codec::{self, Message, PropertyUpdate, TopicInfo};
use crate::error::{Error, Result};
use crate::figure::{Figure, FigureState};
use crate::launcher;
use crate::protocol as p;
use crate::style::PlotStyle;
use crate::transport::{Polled, Transport};

/// Reply timeout of a small request ($SPECTRA_IPC_TIMEOUT overrides, in seconds).
const DEFAULT_IPC_TIMEOUT: f64 = 5.0;
/// Conservative backend throughput used to give large payloads more time.
const IPC_BYTES_PER_SECOND: f64 = 10e6;
/// Extra seconds for an offscreen export (software Vulkan renders slowly).
const EXPORT_EXTRA_TIMEOUT: f64 = 60.0;
/// How long to wait for a freshly launched backend.
const LAUNCH_TIMEOUT: Duration = Duration::from_secs(10);

pub(crate) fn lock<T: ?Sized>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

/// Something the backend told us outside of a request/response pair.
#[derive(Debug, Clone, PartialEq)]
pub enum Event {
    /// A figure's window was closed by the user (or the backend).
    WindowClosed {
        /// Figure shown in the window.
        figure_id: u64,
        /// Window that closed.
        window_id: u64,
        /// Why it closed.
        reason: String,
    },
    /// A figure was destroyed.
    FigureDestroyed {
        /// The destroyed figure.
        figure_id: u64,
    },
    /// Topics appeared or disappeared on the broker.
    TopicListChanged,
}

/// One frame of an animation started with [`Figure::animate`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct AnimFrame {
    /// Figure the animation belongs to.
    pub figure_id: u64,
    /// Frame counter, from 0.
    pub frame: u32,
    /// Seconds since the animation started.
    pub t: f32,
    /// Seconds since the previous frame.
    pub dt: f32,
}

enum Exchange {
    Reply(Message),
    Failed(Error),
    Broken(Error),
}

/// A running [`Figure::animate`] loop: frames are timed here, on the client,
/// and run while the session pumps events.
pub(crate) struct AnimEntry {
    pub on_frame: Box<dyn FnMut(AnimFrame) + Send>,
    pub interval: Duration,
    pub duration: f32,
    pub start: Instant,
    pub last: Instant,
    pub next: Instant,
    pub frame: u32,
    pub running: Arc<AtomicBool>,
}

type AnimHandle = Arc<Mutex<AnimEntry>>;

pub(crate) struct Inner {
    transport: Mutex<Option<Transport>>,
    session_id: AtomicU64,
    next_request_id: AtomicU64,
    socket_path: Mutex<String>,
    figures: Mutex<Vec<Arc<FigureState>>>,
    events: Mutex<VecDeque<Event>>,
    animators: Mutex<HashMap<u64, AnimHandle>>,
    busy_until: Mutex<Instant>,
    data_bytes: AtomicU64,
    closed: AtomicBool,
    default_style: Mutex<Option<PlotStyle>>,
    client_name: String,
}

impl Drop for Inner {
    fn drop(&mut self) {
        self.disconnect();
    }
}

impl Inner {
    fn disconnect(&self) {
        if self.closed.swap(true, Ordering::SeqCst) {
            return;
        }
        let mut t = lock(&self.transport);
        if let Some(tr) = t.as_mut() {
            let _ = tr.send(
                p::REQ_DISCONNECT,
                &[],
                0,
                self.session_id.load(Ordering::SeqCst),
            );
        }
        *t = None;
    }
}

/// Options for [`Session::with_options`].
#[derive(Debug, Clone)]
pub struct SessionOptions {
    /// Broker socket to use. `None` uses `$SPECTRA_SOCKET`, then the newest
    /// running Spectra broker, then launches `spectra-backend`.
    pub socket: Option<String>,
    /// Start `spectra-backend` when no broker is running (default `true`).
    pub auto_launch: bool,
    /// Client name shown in backend logs (it does not change how the backend
    /// treats the connection).
    pub client_name: String,
}

impl Default for SessionOptions {
    fn default() -> Self {
        SessionOptions {
            socket: None,
            auto_launch: true,
            client_name: "rust".to_string(),
        }
    }
}

/// A connection to the Spectra backend (`spectra-backend`).
///
/// Cheap to clone: clones share one connection, and every [`Figure`],
/// [`crate::Axes`] and [`crate::Series`] keeps the session alive. The
/// connection is closed when the last handle is dropped, or by
/// [`Session::close`].
///
/// ```no_run
/// use spectra_plot::Session;
///
/// let session = Session::new()?;
/// let fig = session.figure("Hello")?;
/// let ax = fig.subplot(1, 1, 1)?;
/// ax.plot(&[0.0, 1.0, 2.0], &[0.0, 1.0, 4.0], "r-o")?;
/// session.show()?; // blocks until the window is closed
/// # Ok::<(), spectra_plot::Error>(())
/// ```
#[derive(Clone)]
pub struct Session {
    pub(crate) inner: Arc<Inner>,
}

impl std::fmt::Debug for Session {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Session")
            .field("session_id", &self.session_id())
            .field("socket", &self.socket_path())
            .finish()
    }
}

fn base_timeout() -> f64 {
    env::var("SPECTRA_IPC_TIMEOUT")
        .ok()
        .and_then(|v| v.parse::<f64>().ok())
        .filter(|v| *v > 0.0 && v.is_finite())
        .unwrap_or(DEFAULT_IPC_TIMEOUT)
}

/// HELLO/WELCOME. `role` is the protocol client type the backend routes by
/// ([`p::CLIENT_SCRIPT`] or [`p::CLIENT_PUBLISHER`]); `name` only labels the
/// build string shown in backend logs.
pub(crate) fn handshake(path: &str, role: &str, name: &str) -> Result<(Transport, u64)> {
    let mut t = Transport::connect(path)?;
    let build = format!("spectra-plot-rust/{} ({name})", env!("CARGO_PKG_VERSION"));
    let hello = codec::encode_hello(role, &build);
    t.send(p::HELLO, &hello, 0, 0)?;
    match t.poll(Duration::from_secs_f64(base_timeout()))? {
        Polled::Message(m) if m.header.msg_type == p::WELCOME => {
            Ok((t, codec::decode_welcome(&m.payload)?.session_id))
        }
        Polled::Message(m) => Err(Error::protocol(format!(
            "expected WELCOME, got message type 0x{:04X}",
            m.header.msg_type
        ))),
        Polled::Nothing => Err(Error::Timeout("no WELCOME from the backend".into())),
        Polled::Closed => Err(Error::connection(
            "backend closed the connection during handshake",
        )),
    }
}

/// Connect (launching the backend if allowed), with the same retry policy as Python.
pub(crate) fn connect_with_retry(
    explicit: Option<&str>,
    auto_launch: bool,
    client_name: &str,
) -> Result<(Transport, u64, String)> {
    let attempts = if auto_launch { 3 } else { 1 };
    let mut last: Option<Error> = None;
    for attempt in 1..=attempts {
        let result = (|| {
            let mut path = launcher::resolve_socket_path(explicit);
            if auto_launch {
                path = launcher::ensure_backend(&path, LAUNCH_TIMEOUT)?;
            }
            if path.is_empty() {
                return Err(Error::connection(
                    "no running Spectra backend found (auto_launch is off and SPECTRA_SOCKET is unset)",
                ));
            }
            let (t, sid) = handshake(&path, p::CLIENT_SCRIPT, client_name)?;
            Ok((t, sid, path))
        })();
        match result {
            Ok(v) => return Ok(v),
            Err(e @ Error::BackendNotFound(_)) => return Err(e),
            Err(e) => last = Some(e),
        }
        if attempt < attempts {
            thread::sleep(Duration::from_millis(250 * attempt as u64));
        }
    }
    Err(last.unwrap_or_else(|| Error::connection("could not connect")))
}

impl Session {
    /// Connect to the running Spectra backend, starting `spectra-backend` if needed.
    pub fn new() -> Result<Session> {
        Session::with_options(SessionOptions::default())
    }

    /// Connect to the broker listening on `socket` (no auto-launch elsewhere).
    pub fn connect(socket: &str) -> Result<Session> {
        Session::with_options(SessionOptions {
            socket: Some(socket.to_string()),
            ..Default::default()
        })
    }

    /// Connect with explicit options.
    pub fn with_options(opts: SessionOptions) -> Result<Session> {
        let (transport, sid, path) =
            connect_with_retry(opts.socket.as_deref(), opts.auto_launch, &opts.client_name)?;
        Ok(Session {
            inner: Arc::new(Inner {
                transport: Mutex::new(Some(transport)),
                session_id: AtomicU64::new(sid),
                next_request_id: AtomicU64::new(0),
                socket_path: Mutex::new(path),
                figures: Mutex::new(Vec::new()),
                events: Mutex::new(VecDeque::new()),
                animators: Mutex::new(HashMap::new()),
                busy_until: Mutex::new(Instant::now()),
                data_bytes: AtomicU64::new(0),
                closed: AtomicBool::new(false),
                default_style: Mutex::new(None),
                client_name: opts.client_name.clone(),
            }),
        })
    }

    /// Session id assigned by the backend.
    pub fn session_id(&self) -> u64 {
        self.inner.session_id.load(Ordering::SeqCst)
    }

    /// Socket path of the broker this session talks to.
    pub fn socket_path(&self) -> String {
        lock(&self.inner.socket_path).clone()
    }

    /// Whether the connection is still open.
    pub fn is_connected(&self) -> bool {
        !self.inner.closed.load(Ordering::SeqCst) && lock(&self.inner.transport).is_some()
    }

    /// Create a figure with the default window size (1500 x 900).
    pub fn figure(&self, title: &str) -> Result<Figure> {
        self.figure_sized(title, p::DEFAULT_WINDOW_WIDTH, p::DEFAULT_WINDOW_HEIGHT)
    }

    /// Create a figure with a window size in pixels.
    pub fn figure_sized(&self, title: &str, width: u32, height: u32) -> Result<Figure> {
        if width == 0 || height == 0 {
            return Err(Error::invalid("figure width and height must be > 0"));
        }
        let resp = self.request(
            p::REQ_CREATE_FIGURE,
            &codec::encode_create_figure(title, width, height),
        )?;
        let id = codec::decode_figure_created(&resp.payload)?;
        let state = Arc::new(FigureState::new(id, title, (width, height)));
        lock(&self.inner.figures).push(state.clone());
        let fig = Figure {
            session: self.clone(),
            state,
        };
        let style = *lock(&self.inner.default_style);
        if let Some(style) = style {
            fig.apply_style(style)?;
        }
        Ok(fig)
    }

    /// Figures created through this session that are still alive.
    pub fn figures(&self) -> Vec<Figure> {
        lock(&self.inner.figures)
            .iter()
            .filter(|f| !f.destroyed.load(Ordering::SeqCst))
            .map(|f| Figure {
                session: self.clone(),
                state: f.clone(),
            })
            .collect()
    }

    /// Ask the backend for the ids of every figure in this session.
    pub fn list_figures(&self) -> Result<Vec<u64>> {
        let resp = self.request(p::REQ_LIST_FIGURES, &[])?;
        codec::decode_figure_list(&resp.payload)
    }

    /// Style that every figure created from now on gets (like matplotlib's
    /// `style.use`). `None` or [`PlotStyle::Default`] returns to the default look.
    pub fn set_default_style(&self, style: Option<PlotStyle>) {
        *lock(&self.inner.default_style) = style.filter(|s| *s != PlotStyle::Default);
    }

    /// Show every figure not shown yet, then block until all windows are closed.
    ///
    /// Backend-driven animations ([`Figure::animate`]) run while this blocks.
    pub fn show(&self) -> Result<()> {
        for fig in self.figures() {
            if !fig.state.shown_once.load(Ordering::SeqCst) {
                fig.show()?;
            }
        }
        while self.any_visible() {
            if !self.pump(Duration::from_millis(50))? {
                break;
            }
        }
        Ok(())
    }

    fn any_visible(&self) -> bool {
        lock(&self.inner.figures)
            .iter()
            .any(|f| f.visible.load(Ordering::SeqCst))
    }

    /// Sleep for `duration` while handling backend events (window closes,
    /// animation frames). Use this instead of `std::thread::sleep` in
    /// update loops.
    pub fn sleep(&self, duration: Duration) -> Result<()> {
        let deadline = Instant::now() + duration;
        loop {
            let now = Instant::now();
            if now >= deadline {
                return Ok(());
            }
            let step = (deadline - now).min(Duration::from_millis(20));
            if !self.pump(step)? {
                thread::sleep(deadline.saturating_duration_since(Instant::now()));
                return Ok(());
            }
        }
    }

    /// Handle pending backend messages without blocking and return the
    /// events received since the last call (animation callbacks run first).
    pub fn poll_events(&self) -> Result<Vec<Event>> {
        self.pump(Duration::from_millis(1))?;
        Ok(self.take_events())
    }

    /// Wait up to `timeout` for one message, then run animation callbacks.
    /// Returns `false` when the connection is gone.
    fn pump(&self, timeout: Duration) -> Result<bool> {
        let timeout = match self.next_frame_due() {
            Some(due) => timeout.min(due.saturating_duration_since(Instant::now())),
            None => timeout,
        };
        let alive = {
            match self.inner.transport.try_lock() {
                Ok(mut guard) => match guard.as_mut() {
                    None => false,
                    Some(t) => match t.poll(timeout) {
                        Ok(Polled::Message(m)) => {
                            self.handle_event(&m);
                            true
                        }
                        Ok(Polled::Nothing) => true,
                        Ok(Polled::Closed) | Err(_) => {
                            *guard = None;
                            self.mark_all_hidden();
                            false
                        }
                    },
                },
                // Another thread is mid-request: it will queue our events.
                Err(_) => {
                    thread::sleep(timeout.min(Duration::from_millis(5)));
                    true
                }
            }
        };
        self.dispatch_animations();
        Ok(alive)
    }

    fn mark_all_hidden(&self) {
        for f in lock(&self.inner.figures).iter() {
            f.visible.store(false, Ordering::SeqCst);
        }
    }

    fn take_events(&self) -> Vec<Event> {
        lock(&self.inner.events).drain(..).collect()
    }

    /// When the next animation frame is due, if any animation runs.
    fn next_frame_due(&self) -> Option<Instant> {
        lock(&self.inner.animators)
            .values()
            .filter_map(|a| a.try_lock().ok().map(|a| a.next))
            .min()
    }

    /// Run the animation frames that are due.
    fn dispatch_animations(&self) {
        let handles: Vec<(u64, AnimHandle)> = lock(&self.inner.animators)
            .iter()
            .map(|(k, v)| (*k, v.clone()))
            .collect();
        for (figure_id, handle) in handles {
            // A callback that pumps events itself must not re-enter its own frame.
            let Ok(mut a) = handle.try_lock() else {
                continue;
            };
            let now = Instant::now();
            if now < a.next || !a.running.load(Ordering::SeqCst) {
                continue;
            }
            let t = now.duration_since(a.start).as_secs_f32();
            let frame = AnimFrame {
                figure_id,
                frame: a.frame,
                t,
                dt: now.duration_since(a.last).as_secs_f32(),
            };
            a.frame += 1;
            a.last = now;
            let interval = a.interval;
            a.next += interval;
            if a.next < now {
                a.next = now + interval; // fell behind: skip frames instead of bursting
            }
            (a.on_frame)(frame);
            if a.duration > 0.0 && t >= a.duration {
                a.running.store(false, Ordering::SeqCst);
                drop(a);
                self.unregister_animator(figure_id);
                let _ = self.request(p::REQ_ANIM_STOP, &codec::encode_anim_stop(figure_id));
            }
        }
        // Keep the queue bounded when nobody polls for events.
        let mut q = lock(&self.inner.events);
        while q.len() > 1024 {
            q.pop_front();
        }
    }

    fn handle_event(&self, msg: &Message) {
        let ev = match msg.header.msg_type {
            p::EVT_WINDOW_CLOSED => {
                let (figure_id, window_id, reason) = codec::decode_window_closed(&msg.payload);
                for f in lock(&self.inner.figures)
                    .iter()
                    .filter(|f| f.id == figure_id)
                {
                    f.visible.store(false, Ordering::SeqCst);
                }
                Event::WindowClosed {
                    figure_id,
                    window_id,
                    reason,
                }
            }
            p::EVT_FIGURE_DESTROYED => {
                let figure_id = codec::decode_figure_destroyed(&msg.payload);
                for f in lock(&self.inner.figures)
                    .iter()
                    .filter(|f| f.id == figure_id)
                {
                    f.visible.store(false, Ordering::SeqCst);
                }
                Event::FigureDestroyed { figure_id }
            }
            p::EVT_TOPIC_LIST_CHANGED => Event::TopicListChanged,
            _ => return,
        };
        lock(&self.inner.events).push_back(ev);
    }

    fn reply_timeout(&self, msg_type: u16, payload_len: usize) -> Duration {
        let now = Instant::now();
        let mut busy = lock(&self.inner.busy_until);
        let start = (*busy).max(now);
        *busy = start + Duration::from_secs_f64(payload_len as f64 / IPC_BYTES_PER_SECOND);
        if msg_type == p::REQ_SET_DATA || msg_type == p::REQ_APPEND_DATA {
            self.inner
                .data_bytes
                .fetch_add(payload_len as u64, Ordering::SeqCst);
        }
        let mut secs = base_timeout() + busy.saturating_duration_since(now).as_secs_f64();
        if msg_type == p::REQ_EXPORT_FIGURE {
            secs +=
                EXPORT_EXTRA_TIMEOUT + self.inner.data_bytes.load(Ordering::SeqCst) as f64 / 1e6;
        }
        Duration::from_secs_f64(secs)
    }

    /// Send a request and wait for its reply. Events that arrive meanwhile are queued.
    pub(crate) fn request(&self, msg_type: u16, payload: &[u8]) -> Result<Message> {
        if self.inner.closed.load(Ordering::SeqCst) {
            return Err(Error::connection("session is closed"));
        }
        let timeout = self.reply_timeout(msg_type, payload.len());
        let mut guard = lock(&self.inner.transport);
        let Some(t) = guard.as_mut() else {
            return Err(Error::connection("not connected to the Spectra backend"));
        };
        let req_id = self.inner.next_request_id.fetch_add(1, Ordering::SeqCst) + 1;
        match self.exchange(t, msg_type, payload, req_id, timeout) {
            Exchange::Reply(m) => Ok(m),
            Exchange::Failed(e) => Err(e),
            Exchange::Broken(e) => {
                *guard = None;
                self.mark_all_hidden();
                Err(e)
            }
        }
    }

    fn exchange(
        &self,
        t: &mut Transport,
        msg_type: u16,
        payload: &[u8],
        req_id: u64,
        timeout: Duration,
    ) -> Exchange {
        if let Err(e) = t.send(msg_type, payload, req_id, self.session_id()) {
            return Exchange::Broken(e);
        }
        let deadline = Instant::now() + timeout;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Exchange::Failed(Error::Timeout(format!(
                    "the Spectra backend did not answer {} within {:.0} s ({:.1} MB in this \
                     request). Try again, send fewer points, or allow more time with \
                     SPECTRA_IPC_TIMEOUT=<seconds>",
                    p::message_name(msg_type),
                    timeout.as_secs_f64(),
                    payload.len() as f64 / 1e6
                )));
            }
            let msg = match t.poll(left) {
                Ok(Polled::Message(m)) => m,
                Ok(Polled::Nothing) => continue,
                Ok(Polled::Closed) => {
                    return Exchange::Broken(Error::connection("the backend closed the connection"))
                }
                Err(e) => return Exchange::Broken(e),
            };
            let h = msg.header;
            if h.msg_type == p::RESP_ERR {
                let (rid, code, message) = codec::decode_resp_err(&msg.payload);
                if rid == req_id || rid == 0 || h.request_id == req_id {
                    return Exchange::Failed(Error::Backend { code, message });
                }
            }
            if h.request_id == req_id {
                return Exchange::Reply(msg);
            }
            if h.msg_type == p::RESP_OK && codec::decode_resp_ok(&msg.payload) == req_id {
                return Exchange::Reply(msg);
            }
            self.handle_event(&msg);
        }
    }

    pub(crate) fn update(&self, u: PropertyUpdate) -> Result<()> {
        self.request(p::REQ_UPDATE_PROPERTY, &codec::encode_update_property(&u))
            .map(|_| ())
    }

    pub(crate) fn update_batch(&self, updates: &[PropertyUpdate]) -> Result<()> {
        match updates.len() {
            0 => Ok(()),
            1 => self.update(updates[0].clone()),
            _ => self
                .request(p::REQ_UPDATE_BATCH, &codec::encode_update_batch(updates))
                .map(|_| ()),
        }
    }

    pub(crate) fn register_animator(&self, figure_id: u64, entry: AnimEntry) {
        if let Some(old) =
            lock(&self.inner.animators).insert(figure_id, Arc::new(Mutex::new(entry)))
        {
            if let Ok(old) = old.try_lock() {
                old.running.store(false, Ordering::SeqCst);
            }
        }
    }

    pub(crate) fn unregister_animator(&self, figure_id: u64) {
        lock(&self.inner.animators).remove(&figure_id);
    }

    /// Current values of the interactive knobs (sliders in the window's
    /// PARAMETERS panel), by name. See [`Figure::add_knob`].
    pub fn knob_values(&self) -> Result<HashMap<String, f32>> {
        let resp = self.request(p::REQ_GET_SNAPSHOT, &[])?;
        Ok(codec::decode_snapshot_summary(&resp.payload)?
            .knobs
            .into_iter()
            .collect())
    }

    /// Value of one knob, if it exists.
    pub fn knob(&self, name: &str) -> Result<Option<f32>> {
        Ok(self.knob_values()?.get(name).copied())
    }

    /// Topics currently known to the broker (see [`crate::Publisher`]).
    pub fn list_topics(&self) -> Result<Vec<TopicInfo>> {
        let resp = self.request(p::REQ_LIST_TOPICS, &codec::encode_list_topics())?;
        codec::decode_topic_list(&resp.payload)
    }

    /// Re-attach to an existing backend session (`0` = this session's id)
    /// after the connection dropped. Returns the figure ids the backend still has.
    pub fn reconnect(&self, session_id: u64, token: &str) -> Result<Vec<u64>> {
        {
            let mut guard = lock(&self.inner.transport);
            if guard.is_none() {
                let path = self.socket_path();
                let (t, sid) = handshake(&path, p::CLIENT_SCRIPT, &self.inner.client_name)?;
                *guard = Some(t);
                self.inner.session_id.store(sid, Ordering::SeqCst);
            }
        }
        self.inner.closed.store(false, Ordering::SeqCst);
        let sid = if session_id != 0 {
            session_id
        } else {
            self.session_id()
        };
        let resp = self.request(p::REQ_RECONNECT, &codec::encode_reconnect(sid, token))?;
        if resp.header.msg_type == p::RESP_SNAPSHOT || resp.header.msg_type == p::STATE_SNAPSHOT {
            return Ok(codec::decode_snapshot_summary(&resp.payload)?.figure_ids);
        }
        Ok(Vec::new())
    }

    /// Disconnect from the backend. Figures shown in windows stay open in
    /// the Spectra app; further calls on this session fail.
    pub fn close(&self) {
        lock(&self.inner.animators).clear();
        self.inner.disconnect();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn timeout_env_parsing_defaults() {
        assert!(base_timeout() > 0.0);
    }
}
