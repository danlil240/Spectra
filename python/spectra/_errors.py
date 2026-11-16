"""Exception hierarchy for the Spectra Python client."""


class SpectraError(Exception):
    """Base exception for all Spectra errors."""
    pass


class ConnectionError(SpectraError):
    """Failed to connect to or communicate with the backend."""
    pass


class ProtocolError(SpectraError):
    """Wire protocol violation (bad magic, unknown message type, decode failure)."""
    pass


class TimeoutError(ConnectionError):
    """A request to the backend timed out.

    Subclass of :class:`ConnectionError` so code that caught the raw timeout
    (reported as ConnectionError before) keeps working.
    """
    pass


class FigureNotFoundError(SpectraError):
    """Referenced figure does not exist in the backend."""
    pass


class BackendError(SpectraError):
    """The backend returned an error response."""

    def __init__(self, code: int, message: str):
        self.code = code
        super().__init__(f"Backend error {code}: {message}")
