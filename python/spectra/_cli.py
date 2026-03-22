"""Command-line entry points for spectra."""

import os
import sys


def backend_main():
    """Entry point for 'spectra-backend' console script.

    Forwards all arguments to the bundled spectra-backend binary.
    Supports --download to pre-fetch the binary without running it.
    """
    # Handle --download: fetch binary and exit
    if len(sys.argv) > 1 and sys.argv[1] == "--download":
        from ._download import download_backend
        try:
            path = download_backend()
            print(f"spectra-backend ready: {path}")
        except Exception as exc:
            print(f"Download failed: {exc}", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)

    from ._launcher import _find_backend_binary

    binary = _find_backend_binary()

    # If not found locally, try auto-download
    if binary is None:
        try:
            from ._download import download_backend
            download_backend()
            binary = _find_backend_binary()
        except Exception as exc:
            print(f"Auto-download failed: {exc}", file=sys.stderr)

    if binary is None:
        print(
            "spectra-backend native binary not found.\n"
            "Install the spectra-plot wheel (includes the binary), "
            "set SPECTRA_BACKEND_PATH, or build with CMake "
            "(cmake -B build -DSPECTRA_RUNTIME_MODE=multiproc).\n"
            "Or run: spectra-backend --download",
            file=sys.stderr,
        )
        sys.exit(1)

    # Safety: the binary must be a native executable, not this script.
    # _find_backend_binary already filters non-native binaries, but
    # guard against future regressions that could cause an exec loop.
    resolved = os.path.realpath(binary)
    this_script = os.path.realpath(sys.argv[0])
    if resolved == this_script:
        print(
            "spectra-backend: refusing to exec self (would loop). "
            "Set SPECTRA_BACKEND_PATH to the native binary.",
            file=sys.stderr,
        )
        sys.exit(1)

    os.execv(binary, [binary] + sys.argv[1:])
