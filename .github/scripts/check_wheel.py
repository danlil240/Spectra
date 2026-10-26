"""Smoke test run by cibuildwheel (CIBW_TEST_COMMAND) against an installed wheel.

Wheel test environments have no GPU, so this only checks that the package
imports, reports the expected version, and ships the native multiproc binaries.

usage: python check_wheel.py <path/to/version.txt>
"""

import os
import re
import sys

import spectra

EXE = ".exe" if os.name == "nt" else ""


def main() -> int:
    errors = []

    version = spectra.__version__
    print(f"spectra {version} from {spectra.__file__}")

    if len(sys.argv) > 1:
        with open(sys.argv[1], encoding="utf-8") as fh:
            m = re.search(r"\d+\.\d+\.\d+", fh.read())
        expected = m.group(0) if m else None
        if expected != version:
            errors.append(f"spectra.__version__ is {version!r}, version.txt says {expected!r}")

    pkg_dir = os.path.dirname(os.path.abspath(spectra.__file__))
    for name in ("spectra-backend", "spectra-window"):
        path = os.path.join(pkg_dir, "_bin", name + EXE)
        if not os.path.isfile(path):
            errors.append(f"missing bundled binary: {path}")
        elif not os.access(path, os.X_OK):
            errors.append(f"bundled binary is not executable: {path}")
        else:
            print(f"found {path} ({os.path.getsize(path)} bytes)")

    for err in errors:
        print(f"ERROR: {err}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
