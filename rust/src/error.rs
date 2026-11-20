//! Error type shared by every fallible call in the crate.

use std::fmt;
use std::io;

/// Result alias used throughout `spectra_plot`.
pub type Result<T> = std::result::Result<T, Error>;

/// Everything that can go wrong while talking to the Spectra backend.
#[derive(Debug)]
pub enum Error {
    /// The `spectra-backend` executable could not be found or started.
    BackendNotFound(String),
    /// Connecting to, or talking over, the backend socket failed.
    Connection(String),
    /// The backend did not answer in time.
    Timeout(String),
    /// The backend sent bytes that do not follow the Spectra IPC protocol.
    Protocol(String),
    /// The backend refused a request (`RESP_ERR`).
    Backend {
        /// Numeric error code chosen by the backend.
        code: u32,
        /// Human-readable reason from the backend.
        message: String,
    },
    /// An argument was rejected before anything was sent.
    InvalidArgument(String),
    /// A local I/O error (reading an exported file, spawning a process, ...).
    Io(io::Error),
}

impl Error {
    pub(crate) fn invalid(msg: impl Into<String>) -> Self {
        Error::InvalidArgument(msg.into())
    }

    pub(crate) fn protocol(msg: impl Into<String>) -> Self {
        Error::Protocol(msg.into())
    }

    pub(crate) fn connection(msg: impl Into<String>) -> Self {
        Error::Connection(msg.into())
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::BackendNotFound(m) => write!(f, "spectra backend not available: {m}"),
            Error::Connection(m) => write!(f, "connection to the spectra backend failed: {m}"),
            Error::Timeout(m) => write!(f, "spectra backend timed out: {m}"),
            Error::Protocol(m) => write!(f, "spectra protocol error: {m}"),
            Error::Backend { code, message } => {
                write!(f, "spectra backend error {code}: {message}")
            }
            Error::InvalidArgument(m) => write!(f, "invalid argument: {m}"),
            Error::Io(e) => write!(f, "I/O error: {e}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            _ => None,
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}
