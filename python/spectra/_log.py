"""Spectra Python client logger.

This module provides a shared logger used by all Spectra Python client
components (session, transport, launcher, live threads, etc.).

Environment configuration:
    SPECTRA_LOG=DEBUG|INFO|WARNING|ERROR|CRITICAL
    SPECTRA_LOG=1/TRUE   (alias for DEBUG)
    SPECTRA_LOG=0/FALSE  (alias for WARNING)

Programmatic configuration:
    import spectra as sp
    sp.set_log_level("DEBUG")
"""

import logging
import os
from typing import Union

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

def _ensure_handler() -> None:
    if log.handlers:
        return
    handler = logging.StreamHandler()
    formatter = ColoredFormatter(
        "[spectra %(levelname)s] %(message)s (%(filename)s:%(lineno)d)"
    )
    formatter.handler = handler  # Give formatter access to handler for TTY check
    handler.setFormatter(formatter)
    log.addHandler(handler)
    # Avoid duplicate lines from root logger handlers.
    log.propagate = False


def _normalize_level(level: Union[str, int]) -> int:
    if isinstance(level, int):
        return level

    level_str = str(level).strip().upper()
    aliases = {
        "1": "DEBUG",
        "0": "WARNING",
        "TRUE": "DEBUG",
        "FALSE": "WARNING",
    }
    level_str = aliases.get(level_str, level_str)

    resolved = getattr(logging, level_str, None)
    if not isinstance(resolved, int):
        raise ValueError(
            f"Invalid log level: {level!r}. "
            "Use DEBUG/INFO/WARNING/ERROR/CRITICAL or numeric levels."
        )
    return resolved


def set_log_level(level: Union[str, int]) -> int:
    """Set Spectra Python client log level and ensure handler is active.

    Returns the resolved numeric logging level.
    """
    resolved = _normalize_level(level)
    _ensure_handler()
    log.setLevel(resolved)
    log.debug("logger level set to %s", logging.getLevelName(resolved))
    return resolved


def get_log_level() -> int:
    """Return current Spectra logger level."""
    return log.level


# Configure from SPECTRA_LOG env var if set.
_env_level = os.environ.get("SPECTRA_LOG", "").strip()
if _env_level:
    try:
        set_log_level(_env_level)
    except ValueError:
        # Keep silent defaults for invalid env values.
        pass
