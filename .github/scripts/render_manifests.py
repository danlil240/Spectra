"""Render package-manager manifests for a release with real version + sha256.

The templates in packaging/ carry placeholder versions and hashes. Homebrew and
AUR build from the GitHub source tarball of the tag, so both get that tarball's
sha256. The rendered files are attached to the GitHub release; the templates in
the repository are left untouched.

Scoop is intentionally not rendered: packaging/scoop/spectra.json only installs
spectra-backend.exe / spectra-window.exe (the multiproc Python backend), which
does not work on Windows ("Unix domain socket daemon not supported").

usage: render_manifests.py --version X.Y.Z --tag vX.Y.Z --source-sha256 HEX
                           --repo owner/name --out DIR
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sub_once(pattern: str, repl: str, text: str, what: str) -> str:
    new, n = re.subn(pattern, repl, text, count=1, flags=re.M)
    if n != 1:
        sys.exit(f"render_manifests: could not find {what}")
    return new


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--source-sha256", required=True)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    if not re.fullmatch(r"[0-9a-f]{64}", a.source_sha256):
        sys.exit("render_manifests: --source-sha256 must be 64 lowercase hex chars")
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    tarball = f"https://github.com/{a.repo}/archive/refs/tags/{a.tag}.tar.gz"

    # Homebrew formula
    rb = (ROOT / "packaging/homebrew/spectra.rb").read_text()
    rb = sub_once(r'^(\s*url\s+)"[^"]*"', rf'\g<1>"{tarball}"', rb, "homebrew url")
    rb = sub_once(r'^(\s*sha256\s+)"[^"]*"', rf'\g<1>"{a.source_sha256}"', rb, "homebrew sha256")
    (out / "spectra.rb").write_text(rb)

    # AUR PKGBUILD (source= uses $pkgver and the v$pkgver tag)
    pkg = (ROOT / "packaging/aur/PKGBUILD").read_text()
    pkg = sub_once(r"^pkgver=.*$", f"pkgver={a.version}", pkg, "PKGBUILD pkgver")
    pkg = sub_once(r"^pkgrel=.*$", "pkgrel=1", pkg, "PKGBUILD pkgrel")
    pkg = sub_once(r"^sha256sums=\(.*\)$", f"sha256sums=('{a.source_sha256}')", pkg,
                   "PKGBUILD sha256sums")
    if f"v$pkgver" not in pkg or a.tag != f"v{a.version}":
        sys.exit("render_manifests: PKGBUILD source expects tag v$pkgver; tag/version mismatch")
    (out / "PKGBUILD").write_text(pkg)

    for f in sorted(out.iterdir()):
        print(f"rendered {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
