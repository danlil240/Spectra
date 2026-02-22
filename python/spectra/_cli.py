"""Command-line entry points for spectra."""

import os
import sys


def backend_main():
    """Entry point for 'spectra-backend' console script.

    Forwards all arguments to the bundled spectra-backend binary.
    """
    from ._launcher import _find_backend_binary

    binary = _find_backend_binary()
    if binary is None:
        print(
            "spectra-backend binary not found. "
            "Set SPECTRA_BACKEND_PATH or rebuild with CMake.",
            file=sys.stderr,
        )
        sys.exit(1)

    os.execv(binary, [binary] + sys.argv[1:])
