"""Explicit fallback: fetch pre-built backend binaries from GitHub Releases.

Platform wheels (``pip install spectra-plot``) bundle ``spectra-backend`` and
``spectra-window`` in ``spectra/_bin/`` and never need this module.  It exists
for installs without bundled binaries (e.g. a pure-Python editable install on a
machine without a local C++ build) and is only used when explicitly requested:

* ``spectra-backend --download``  (console script), or
* ``SPECTRA_AUTO_DOWNLOAD=1``     (let the launcher download on first use).

Integrity rules — a downloaded binary is never executed unverified:

1. The release is pinned to the installed package version (tag ``v<version>``).
2. The release's ``SHA256SUMS`` file (produced by ``release.yml``) is fetched
   and the archive's SHA-256 must match its entry; a missing entry or any
   mismatch aborts the download and nothing is extracted.
3. The cache is per version and records the SHA-256 of every extracted binary;
   cached binaries are re-hashed before use and ignored if they changed.

Note: ``SHA256SUMS`` is served from the same release as the archive, so this
protects against corruption/truncation and tampering with individual assets.
For authenticity, verify ``SHA256SUMS.asc`` with the release signing key
(see the release notes).

Cache location (respects XDG / platform conventions):
  Linux   : $XDG_DATA_HOME/spectra/bin/<version>/ or ~/.local/share/spectra/bin/<version>/
  macOS   : ~/Library/Application Support/spectra/bin/<version>/
  Windows : %LOCALAPPDATA%/spectra/bin/<version>/
"""

import hashlib
import io
import json
import os
import platform
import shutil
import stat
import sys
import tarfile
import tempfile
import zipfile
from typing import Dict, Optional, Tuple
from urllib.error import URLError
from urllib.request import Request, urlopen

# ── Constants ────────────────────────────────────────────────────────────────

GITHUB_REPO = "danlil240/Spectra"
_ASSET_PREFIX = "spectra-backend-"
_CHECKSUMS_NAME = "SHA256SUMS"
_MANIFEST_NAME = "verified.json"

# Map (system, machine) to the asset suffix used in GitHub Releases.
_PLATFORM_MAP = {
    ("Linux", "x86_64"): "linux-x86_64",
    ("Linux", "aarch64"): "linux-aarch64",
    ("Darwin", "arm64"): "macos-arm64",
    ("Darwin", "x86_64"): "macos-x86_64",
    ("Windows", "AMD64"): "windows-x86_64",
}

_BINARY_NAMES = ("spectra-backend", "spectra-window")


class ChecksumError(RuntimeError):
    """Raised when a downloaded artifact does not match the published SHA-256."""


def _exe_suffix() -> str:
    return ".exe" if platform.system() == "Windows" else ""


def _detect_platform() -> str:
    """Return the platform tag (e.g. 'linux-x86_64')."""
    key = (platform.system(), platform.machine())
    tag = _PLATFORM_MAP.get(key)
    if tag is None:
        raise RuntimeError(
            f"No pre-built spectra-backend available for {key[0]}/{key[1]}. "
            "Build from source: cmake -B build -DSPECTRA_RUNTIME_MODE=multiproc"
        )
    return tag


def _cache_root() -> str:
    """Return the platform-appropriate cache directory for Spectra binaries."""
    system = platform.system()
    if system == "Linux":
        base = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
    elif system == "Darwin":
        base = os.path.expanduser("~/Library/Application Support")
    elif system == "Windows":
        base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local"))
    else:
        base = os.path.expanduser("~/.local/share")
    return os.path.join(base, "spectra", "bin")


def _cache_dir(version: Optional[str] = None) -> str:
    """Per-version cache directory (``<cache root>/<version>``)."""
    ver = version or _read_version()
    return os.path.join(_cache_root(), ver or "unknown")


def _read_version() -> str:
    """Return the installed package version (release tags are ``v<version>``)."""
    try:
        from spectra import __version__

        if __version__ and "unknown" not in __version__:
            return __version__
    except Exception:
        pass
    return ""


def _asset_url(tag: str, plat: str) -> Tuple[str, str]:
    """Return (url, archive_name) for the backend archive of release ``v<tag>``.

    Archive naming convention set by release.yml (backend-binaries job):
      spectra-backend-{platform}.tar.gz   (Linux/macOS)
      spectra-backend-{platform}.zip      (Windows)
    """
    ext = "zip" if plat.startswith("windows") else "tar.gz"
    name = f"{_ASSET_PREFIX}{plat}.{ext}"
    return _release_url(tag, name), name


def _release_url(tag: str, name: str) -> str:
    return f"https://github.com/{GITHUB_REPO}/releases/download/v{tag}/{name}"


def _fetch(url: str) -> bytes:
    req = Request(url, headers={"Accept": "application/octet-stream"})
    try:
        with urlopen(req, timeout=60) as resp:  # noqa: S310 — https GitHub URL
            return resp.read()
    except URLError as exc:
        raise RuntimeError(f"Failed to download {url}: {exc}") from exc


def _parse_sha256sums(text: str) -> Dict[str, str]:
    """Parse ``sha256sum`` output (``<hex>  <name>`` or ``<hex> *<name>``)."""
    sums: Dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts
        name = name.lstrip("*").strip()
        digest = digest.lower()
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            continue
        sums[os.path.basename(name)] = digest
    return sums


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _verify_sha256(data: bytes, expected: str, name: str) -> None:
    actual = _sha256(data)
    if actual != expected.lower():
        raise ChecksumError(
            f"SHA-256 mismatch for {name}: expected {expected}, got {actual}. "
            "Refusing to install the downloaded spectra-backend."
        )


def _make_executable(path: str) -> None:
    """chmod +x on non-Windows."""
    if platform.system() != "Windows":
        st = os.stat(path)
        os.chmod(path, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def _extract_binaries(data: bytes, archive_name: str, dest_dir: str) -> Dict[str, str]:
    """Extract only the known binaries (flattened, regular files) into *dest_dir*.

    Returns ``{binary_name: sha256}`` for the extracted files.
    """
    allowed = {n + _exe_suffix() for n in _BINARY_NAMES} | set(_BINARY_NAMES)
    os.makedirs(dest_dir, mode=0o755, exist_ok=True)
    written: Dict[str, str] = {}

    def _write(basename: str, payload: bytes) -> None:
        target = os.path.join(dest_dir, basename)
        with open(target, "wb") as dst:
            dst.write(payload)
        _make_executable(target)
        written[basename] = _sha256(payload)

    if archive_name.endswith(".tar.gz"):
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
            for member in tf.getmembers():
                basename = os.path.basename(member.name)
                # Regular files only: never follow symlinks/hardlinks/devices,
                # and never honour directory components (path traversal).
                if basename not in allowed or not member.isfile():
                    continue
                src = tf.extractfile(member)
                if src is not None:
                    _write(basename, src.read())
    elif archive_name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as zf:
            for info in zf.infolist():
                basename = os.path.basename(info.filename)
                if basename not in allowed or info.is_dir():
                    continue
                with zf.open(info) as src:
                    _write(basename, src.read())
    else:
        raise RuntimeError(f"Unknown archive format: {archive_name}")
    return written


def _load_manifest(cache: str) -> Optional[dict]:
    try:
        with open(os.path.join(cache, _MANIFEST_NAME), encoding="utf-8") as f:
            manifest = json.load(f)
        return manifest if isinstance(manifest, dict) else None
    except (OSError, ValueError):
        return None


def find_cached_backend(version: Optional[str] = None) -> Optional[str]:
    """Return a previously downloaded *and verified* backend for this version.

    Every cached binary is re-hashed against the manifest written at download
    time; a missing manifest or any mismatch means the cache is ignored.
    """
    ver = version or _read_version()
    if not ver:
        return None
    cache = _cache_dir(ver)
    manifest = _load_manifest(cache)
    if not manifest or manifest.get("version") != ver:
        return None
    files = manifest.get("files")
    if not isinstance(files, dict):
        return None
    backend_name = f"spectra-backend{_exe_suffix()}"
    if backend_name not in files:
        return None
    for name, digest in files.items():
        path = os.path.join(cache, os.path.basename(name))
        try:
            if _sha256_file(path) != digest:
                return None
        except OSError:
            return None
    backend = os.path.join(cache, backend_name)
    if os.access(backend, os.X_OK):
        return backend
    return None


def download_backend(version: Optional[str] = None, quiet: bool = False) -> str:
    """Download, verify and cache the backend binaries for this platform.

    The release is pinned to the installed package version unless *version* is
    given.  Returns the path to the verified ``spectra-backend`` binary.
    Raises :class:`ChecksumError` on checksum mismatch and ``RuntimeError`` on
    any other failure; nothing unverified is ever left in the cache.
    """
    plat = _detect_platform()
    ver = version or _read_version()
    if not ver:
        raise RuntimeError(
            "Cannot determine the spectra-plot version to download. "
            "Set SPECTRA_BACKEND_PATH or install a platform wheel."
        )

    cached = find_cached_backend(ver)
    if cached:
        return cached

    url, archive_name = _asset_url(ver, plat)
    sums_url = _release_url(ver, _CHECKSUMS_NAME)
    if not quiet:
        print(f"[spectra] Downloading backend for {plat} (v{ver}) from {url}", file=sys.stderr)

    sums = _parse_sha256sums(_fetch(sums_url).decode("utf-8", errors="replace"))
    expected = sums.get(archive_name)
    if not expected:
        raise ChecksumError(
            f"{_CHECKSUMS_NAME} of release v{ver} has no entry for {archive_name}; "
            "refusing to install an unverified spectra-backend."
        )

    data = _fetch(url)
    _verify_sha256(data, expected, archive_name)

    cache = _cache_dir(ver)
    root = os.path.dirname(cache)
    os.makedirs(root, mode=0o755, exist_ok=True)
    staging = tempfile.mkdtemp(prefix=f".{ver}-", dir=root)
    try:
        files = _extract_binaries(data, archive_name, staging)
        if f"spectra-backend{_exe_suffix()}" not in files:
            raise RuntimeError(
                f"{archive_name} does not contain spectra-backend. "
                "This is a packaging bug — please file an issue."
            )
        manifest = {
            "version": ver,
            "platform": plat,
            "archive": archive_name,
            "archive_sha256": expected,
            "files": files,
        }
        with open(os.path.join(staging, _MANIFEST_NAME), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
        if os.path.isdir(cache):
            shutil.rmtree(cache)
        os.replace(staging, cache)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    result = find_cached_backend(ver)
    if result is None:
        raise RuntimeError(f"Verified download installed to {cache} but cannot be used.")
    if not quiet:
        print(f"[spectra] Backend verified (sha256 {expected[:12]}…): {result}", file=sys.stderr)
    return result


def auto_download_enabled() -> bool:
    """Whether the launcher may download on first use (opt-in)."""
    if os.environ.get("SPECTRA_NO_DOWNLOAD", "").lower() in ("1", "true", "yes"):
        return False
    return os.environ.get("SPECTRA_AUTO_DOWNLOAD", "").lower() in ("1", "true", "yes")
