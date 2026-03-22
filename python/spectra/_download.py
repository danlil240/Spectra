"""Download pre-built spectra-backend binaries from GitHub Releases.

When spectra-plot is installed as a pure-Python package (e.g. via sdist or
editable install), no compiled backend is bundled.  This module downloads the
correct platform-specific binary on first use and caches it locally.

Cache location (respects XDG / platform conventions):
  Linux   : $XDG_DATA_HOME/spectra/bin/  or ~/.local/share/spectra/bin/
  macOS   : ~/Library/Application Support/spectra/bin/
  Windows : %LOCALAPPDATA%/spectra/bin/
"""

import io
import os
import platform
import stat
import sys
import tarfile
import zipfile
from typing import Optional, Tuple
from urllib.error import URLError
from urllib.request import urlopen, Request

# ── Constants ────────────────────────────────────────────────────────────────

GITHUB_REPO = "danlil240/Spectra"
_ASSET_PREFIX = "spectra-backend-"

# Map (system, machine) to the asset suffix used in GitHub Releases.
_PLATFORM_MAP = {
    ("Linux", "x86_64"): "linux-x86_64",
    ("Linux", "aarch64"): "linux-aarch64",
    ("Darwin", "arm64"): "macos-arm64",
    ("Darwin", "x86_64"): "macos-x86_64",
    ("Windows", "AMD64"): "windows-x86_64",
}


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


def _cache_dir() -> str:
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


def _read_version() -> str:
    """Read the package version to match against GitHub Release tags."""
    try:
        from importlib.metadata import version
        return version("spectra-plot")
    except Exception:
        pass
    # Fallback: read VERSION file shipped next to this module
    version_file = os.path.join(os.path.dirname(__file__), "..", "VERSION")
    if os.path.isfile(version_file):
        return open(version_file).read().strip()
    return ""


def _asset_url(tag: str, plat: str) -> Tuple[str, str]:
    """Return (url, archive_name) for the asset.

    Archive naming convention set by CI:
      spectra-backend-{platform}.tar.gz   (Linux/macOS)
      spectra-backend-{platform}.zip      (Windows)
    """
    ext = "zip" if plat.startswith("windows") else "tar.gz"
    name = f"{_ASSET_PREFIX}{plat}.{ext}"
    url = f"https://github.com/{GITHUB_REPO}/releases/download/v{tag}/{name}"
    return url, name


def _download_and_extract(url: str, dest_dir: str) -> None:
    """Download an archive from *url* and extract binaries into *dest_dir*."""
    req = Request(url, headers={"Accept": "application/octet-stream"})
    try:
        resp = urlopen(req, timeout=60)  # noqa: S310 — URL is hardcoded to GitHub
        data = resp.read()
    except URLError as exc:
        raise RuntimeError(
            f"Failed to download spectra-backend from {url}: {exc}"
        ) from exc

    os.makedirs(dest_dir, mode=0o755, exist_ok=True)

    if url.endswith(".tar.gz"):
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
            # Only extract known binary names to avoid path traversal
            for member in tf.getmembers():
                basename = os.path.basename(member.name)
                if basename in ("spectra-backend", "spectra-window"):
                    member.name = basename  # flatten into dest_dir
                    tf.extract(member, dest_dir)
                    _make_executable(os.path.join(dest_dir, basename))
    elif url.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as zf:
            for info in zf.infolist():
                basename = os.path.basename(info.filename)
                if basename in ("spectra-backend.exe", "spectra-window.exe",
                                "spectra-backend", "spectra-window"):
                    target = os.path.join(dest_dir, basename)
                    with zf.open(info) as src, open(target, "wb") as dst:
                        dst.write(src.read())
                    _make_executable(target)
    else:
        raise RuntimeError(f"Unknown archive format: {url}")


def _make_executable(path: str) -> None:
    """chmod +x on non-Windows."""
    if platform.system() != "Windows":
        st = os.stat(path)
        os.chmod(path, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def _version_marker(cache: str) -> str:
    return os.path.join(cache, ".version")


def find_cached_backend() -> Optional[str]:
    """Return path to a cached spectra-backend binary, or None."""
    cache = _cache_dir()
    suffix = ".exe" if platform.system() == "Windows" else ""
    backend = os.path.join(cache, f"spectra-backend{suffix}")
    if os.path.isfile(backend) and os.access(backend, os.X_OK):
        return backend
    return None


def download_backend(version: Optional[str] = None) -> str:
    """Download the spectra-backend binary for this platform.

    Returns the path to the downloaded binary.
    Raises RuntimeError on failure.
    """
    plat = _detect_platform()
    ver = version or _read_version()
    if not ver:
        raise RuntimeError(
            "Cannot determine spectra-plot version for download. "
            "Set SPECTRA_BACKEND_PATH or install a platform wheel."
        )

    cache = _cache_dir()
    marker = _version_marker(cache)

    # Skip download if version already matches
    if os.path.isfile(marker):
        cached_ver = open(marker).read().strip()
        cached_bin = find_cached_backend()
        if cached_ver == ver and cached_bin:
            return cached_bin

    url, _ = _asset_url(ver, plat)
    print(
        f"[spectra] Downloading backend for {plat} (v{ver})...",
        file=sys.stderr,
    )
    _download_and_extract(url, cache)

    # Record version
    with open(marker, "w") as f:
        f.write(ver)

    result = find_cached_backend()
    if result is None:
        raise RuntimeError(
            f"Download succeeded but spectra-backend not found in {cache}. "
            "This is a packaging bug — please file an issue."
        )
    print(f"[spectra] Backend ready: {result}", file=sys.stderr)
    return result
