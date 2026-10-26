#!/usr/bin/env python3
"""Smoke test for an *installed* spectra-plot wheel (not collected by pytest).

Run it from outside the source tree with an isolated interpreter so the repo
is never on sys.path, e.g.:

    cd /tmp && SPECTRA_NO_DOWNLOAD=1 xvfb-run -a python -I /path/to/wheel_smoke.py

It checks that:
  * the package imports (runtime deps such as flatbuffers are installed),
  * the bundled spectra-backend and spectra-window are present and executable,
  * the launcher resolves the bundled backend (no download fallback),
  * a figure can be created, plotted and shown through the bundled daemon.
"""
import os
import sys
import time


def main() -> int:
    import spectra as sp
    from spectra import _launcher

    pkg_dir = os.path.dirname(os.path.abspath(sp.__file__))
    if "site-packages" not in pkg_dir and "dist-packages" not in pkg_dir:
        print(f"FAIL: spectra imported from source tree, not an install: {pkg_dir}")
        return 1
    print(f"spectra {sp.__version__} from {pkg_dir}")

    exe = ".exe" if sys.platform == "win32" else ""
    bin_dir = os.path.join(pkg_dir, "_bin")
    for name in ("spectra-backend", "spectra-window"):
        path = os.path.join(bin_dir, name + exe)
        if not (os.path.isfile(path) and os.access(path, os.X_OK)):
            print(f"FAIL: bundled {name} missing or not executable: {path}")
            return 1

    os.environ.pop("SPECTRA_BACKEND_PATH", None)
    found = _launcher._find_backend_binary()
    expected = os.path.join(bin_dir, "spectra-backend" + exe)
    if found != expected:
        print(f"FAIL: launcher resolved {found!r}, expected bundled {expected!r}")
        return 1
    print(f"bundled backend: {found}")

    s = sp.Session()
    fig = s.figure("wheel smoke")
    ax = fig.subplot(1, 1, 1)
    ax.line([0.0, 1.0, 2.0, 3.0], [0.0, 1.0, 4.0, 9.0], label="smoke")
    fig.show()
    if fig.id not in s.list_figures():
        print(f"FAIL: figure {fig.id} not known to backend: {s.list_figures()}")
        return 1

    # The daemon must have spawned the *bundled* window agent.
    agent = os.path.join(bin_dir, "spectra-window" + exe)
    if sys.platform.startswith("linux"):
        deadline = time.monotonic() + 15.0
        seen = False
        while time.monotonic() < deadline and not seen:
            for pid in filter(str.isdigit, os.listdir("/proc")):
                try:
                    if os.path.realpath(f"/proc/{pid}/exe") == os.path.realpath(agent):
                        seen = True
                        break
                except OSError:
                    continue
            time.sleep(0.25)
        if not seen:
            print(f"FAIL: bundled window agent never started: {agent}")
            return 1
        print(f"window agent running: {agent}")
    time.sleep(1.0)
    s.close()
    print("SMOKE OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
