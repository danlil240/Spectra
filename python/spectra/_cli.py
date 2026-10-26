"""Command-line entry points for spectra."""

import os
import platform as _platform
import sys


def backend_main():
    """Entry point for 'spectra-backend' console script.

    Forwards all arguments to the bundled spectra-backend binary.
    ``--download`` explicitly fetches the pre-built binaries for this package
    version from GitHub Releases (SHA-256 verified against the release's
    SHA256SUMS) and exits without running them.
    """
    if len(sys.argv) > 1 and sys.argv[1] == "--download":
        from ._download import download_backend

        try:
            path = download_backend()
            print(f"spectra-backend ready (verified): {path}")
        except Exception as exc:
            print(f"Download failed: {exc}", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)

    from ._download import auto_download_enabled
    from ._launcher import _find_backend_binary

    binary = _find_backend_binary()

    if binary is None and auto_download_enabled():
        try:
            from ._download import download_backend

            download_backend()
            binary = _find_backend_binary()
        except Exception as exc:
            print(f"Verified download failed: {exc}", file=sys.stderr)

    if binary is None:
        print(
            "spectra-backend native binary not found.\n"
            "Install a spectra-plot platform wheel (bundles the binaries), "
            "set SPECTRA_BACKEND_PATH, or build with CMake "
            "(cmake -B build -DSPECTRA_RUNTIME_MODE=multiproc).\n"
            "Or fetch a checksum-verified pre-built binary: spectra-backend --download",
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

    if _platform.system() == "Windows":
        # os.execv on Windows spawns a new process and exits the current one
        # which can cause issues with console handling. Use subprocess instead.
        import subprocess
        sys.exit(subprocess.call([binary] + sys.argv[1:]))
    else:
        os.execv(binary, [binary] + sys.argv[1:])
