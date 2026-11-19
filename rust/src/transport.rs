//! Framed message I/O over a local (Unix domain) socket.

use std::io::{self, Read, Write};
use std::time::Duration;

#[cfg(unix)]
pub(crate) use std::os::unix::net::UnixStream as Stream;
#[cfg(windows)]
pub(crate) use uds_windows::UnixStream as Stream;

use crate::codec::{Header, Message};
use crate::error::{Error, Result};
use crate::protocol as p;

/// Extra time allowed to read the rest of a frame once its first byte arrived.
const MID_FRAME_TIMEOUT: Duration = Duration::from_secs(30);

/// Outcome of a non-blocking-ish receive.
pub(crate) enum Polled {
    /// A whole frame arrived.
    Message(Message),
    /// Nothing arrived within the poll interval; the stream is still in sync.
    Nothing,
    /// The peer closed the connection.
    Closed,
}

/// A connected socket plus the outgoing sequence counter.
pub(crate) struct Transport {
    stream: Stream,
    seq: u64,
}

fn is_timeout(e: &io::Error) -> bool {
    matches!(
        e.kind(),
        io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut
    )
}

impl Transport {
    pub fn connect(path: &str) -> Result<Self> {
        let stream = Stream::connect(path)
            .map_err(|e| Error::connection(format!("cannot connect to {path}: {e}")))?;
        Ok(Transport { stream, seq: 0 })
    }

    /// Send one frame. Returns the sequence number used.
    pub fn send(
        &mut self,
        msg_type: u16,
        payload: &[u8],
        request_id: u64,
        session_id: u64,
    ) -> Result<u64> {
        if payload.len() > p::MAX_PAYLOAD_SIZE {
            return Err(Error::invalid(format!(
                "payload of {} bytes exceeds the {} byte protocol limit",
                payload.len(),
                p::MAX_PAYLOAD_SIZE
            )));
        }
        self.seq += 1;
        let header = Header {
            msg_type,
            payload_len: payload.len() as u32,
            seq: self.seq,
            request_id,
            session_id,
            window_id: 0,
        };
        let _ = self.stream.set_write_timeout(Some(MID_FRAME_TIMEOUT));
        let mut frame = Vec::with_capacity(p::HEADER_SIZE + payload.len().min(1 << 16));
        frame.extend_from_slice(&header.encode());
        if payload.len() <= 1 << 16 {
            frame.extend_from_slice(payload);
            self.stream.write_all(&frame)
        } else {
            self.stream
                .write_all(&frame)
                .and_then(|_| self.stream.write_all(payload))
        }
        .map_err(|e| Error::connection(format!("send failed: {e}")))?;
        Ok(self.seq)
    }

    /// Wait up to `timeout` for a frame.
    ///
    /// A timeout before the first byte leaves the stream in sync and returns
    /// [`Polled::Nothing`]; a frame cut off mid-way is a connection error.
    pub fn poll(&mut self, timeout: Duration) -> Result<Polled> {
        let timeout = timeout.max(Duration::from_millis(1));
        let mut header = [0u8; p::HEADER_SIZE];
        self.stream
            .set_read_timeout(Some(timeout))
            .map_err(Error::Io)?;
        let first = loop {
            match self.stream.read(&mut header[..1]) {
                Ok(n) => break n,
                Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                Err(e) if is_timeout(&e) => return Ok(Polled::Nothing),
                Err(e) => return Err(Error::connection(format!("receive failed: {e}"))),
            }
        };
        if first == 0 {
            return Ok(Polled::Closed);
        }
        self.stream
            .set_read_timeout(Some(MID_FRAME_TIMEOUT.max(timeout)))
            .map_err(Error::Io)?;
        self.read_rest(&mut header[1..])?;
        let header = Header::decode(&header)?;
        let len = header.payload_len as usize;
        if len > p::MAX_PAYLOAD_SIZE {
            return Err(Error::protocol(format!("payload too large: {len} bytes")));
        }
        let mut payload = vec![0u8; len];
        self.read_rest(&mut payload)?;
        Ok(Polled::Message(Message { header, payload }))
    }

    fn read_rest(&mut self, buf: &mut [u8]) -> Result<()> {
        self.stream.read_exact(buf).map_err(|e| {
            if e.kind() == io::ErrorKind::UnexpectedEof {
                Error::connection("connection closed in the middle of a message")
            } else {
                Error::connection(format!("receive failed mid-message: {e}"))
            }
        })
    }

    /// Whether the peer still looks connected (no EOF pending).
    #[cfg(unix)]
    pub fn is_alive(&mut self) -> bool {
        use std::os::fd::AsRawFd;
        let mut b = [0u8; 1];
        // SAFETY: recv() with MSG_PEEK writes at most 1 byte into `b`, which outlives the call.
        let n = unsafe {
            libc::recv(
                self.stream.as_raw_fd(),
                b.as_mut_ptr().cast(),
                1,
                libc::MSG_PEEK | libc::MSG_DONTWAIT,
            )
        };
        if n > 0 {
            return true;
        }
        if n == 0 {
            return false;
        }
        let e = io::Error::last_os_error();
        matches!(
            e.kind(),
            io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
        )
    }

    /// Whether the peer still looks connected (send/receive errors detect drops).
    #[cfg(windows)]
    pub fn is_alive(&mut self) -> bool {
        true
    }
}
