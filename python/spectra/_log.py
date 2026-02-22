"""Spectra Python client logger.

Usage from any module::

    from ._log import log

    log.debug("connecting to %s", path)
    log.warning("socket timeout after %.1fs", elapsed)

Enable via environment variable::

    SPECTRA_LOG=DEBUG python my_script.py   # all messages
    SPECTRA_LOG=INFO  python my_script.py   # info and above
    SPECTRA_LOG=1     python my_script.py   # alias for DEBUG

Or programmatically::

    import logging
    logging.getLogger("spectra").setLevel(logging.DEBUG)
"""

import logging
import os

log = logging.getLogger("spectra")

# ANSI color codes
_COLORS = {
    "DEBUG": "\033[36m",    # Cyan
    "INFO": "\033[32m",     # Green
    "WARNING": "\033[33m",  # Yellow
    "ERROR": "\033[31m",    # Red
    "RESET": "\033[0m",     # Reset
}

class ColoredFormatter(logging.Formatter):
    """Add colors to log levels when output is a TTY."""
    
    def format(self, record):
        # Only add colors when writing to a terminal
        if hasattr(self.handler, 'stream') and hasattr(self.handler.stream, 'isatty') and self.handler.stream.isatty():
            color = _COLORS.get(record.levelname, "")
            reset = _COLORS["RESET"]
            record.levelname = f"{color}{record.levelname}{reset}"
        return super().format(record)

# Configure from SPECTRA_LOG env var if set
_level_str = os.environ.get("SPECTRA_LOG", "").strip().upper()
if _level_str:
    _ALIASES = {"1": "DEBUG", "0": "WARNING", "TRUE": "DEBUG", "FALSE": "WARNING"}
    _level_str = _ALIASES.get(_level_str, _level_str)
    _level = getattr(logging, _level_str, None)
    if isinstance(_level, int):
        log.setLevel(_level)
        if not log.handlers:
            _handler = logging.StreamHandler()
            _formatter = ColoredFormatter(
                "[spectra %(levelname)s] %(message)s (%(filename)s:%(lineno)d)"
            )
            _formatter.handler = _handler  # Give formatter access to handler for TTY check
            _handler.setFormatter(_formatter)
            log.addHandler(_handler)
