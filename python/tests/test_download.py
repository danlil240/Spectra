#!/usr/bin/env python3
"""Tests for the explicit, checksum-verified backend download fallback."""
import hashlib
import io
import os
import platform
import tarfile

import pytest

import spectra._download as dl
from spectra._download import (
    ChecksumError,
    _asset_url,
    _detect_platform,
    _extract_binaries,
    _parse_sha256sums,
    _read_version,
    download_backend,
    find_cached_backend,
)

_IS_LINUX_X64 = (platform.system(), platform.machine()) == ("Linux", "x86_64")


def _tar_gz(members):
    """Build a .tar.gz in memory from {name: bytes} (or TarInfo tweaks)."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        for name, data in members.items():
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            info.mode = 0o755
            tf.addfile(info, io.BytesIO(data))
    return buf.getvalue()


@pytest.fixture
def cache_root(tmp_path, monkeypatch):
    root = tmp_path / "cache"
    monkeypatch.setattr(dl, "_cache_root", lambda: str(root))
    return root


@pytest.fixture
def fake_release(monkeypatch):
    """Serve a fake GitHub release: {url_basename: bytes}; records fetched URLs."""
    assets = {}
    fetched = []

    def _fetch(url):
        fetched.append(url)
        name = url.rsplit("/", 1)[-1]
        if name not in assets:
            raise RuntimeError(f"404 {url}")
        return assets[name]

    monkeypatch.setattr(dl, "_fetch", _fetch)
    return assets, fetched


@pytest.mark.skipif(not _IS_LINUX_X64, reason="platform-specific expectation")
def test_platform_detection():
    assert _detect_platform() == "linux-x86_64"


def test_cache_directory_is_per_version(cache_root):
    assert dl._cache_dir("1.2.3") == os.path.join(str(cache_root), "1.2.3")


def test_asset_url_linux():
    url, name = _asset_url("0.2.1", "linux-x86_64")
    assert url == (
        "https://github.com/danlil240/Spectra/releases/download/v0.2.1/"
        "spectra-backend-linux-x86_64.tar.gz"
    )
    assert name == "spectra-backend-linux-x86_64.tar.gz"


def test_asset_url_windows():
    url_w, name_w = _asset_url("0.2.1", "windows-x86_64")
    assert url_w.endswith(".zip")
    assert name_w.endswith(".zip")


def test_version_matches_package():
    import spectra

    assert _read_version() == spectra.__version__


def test_parse_sha256sums_formats():
    h1, h2 = "a" * 64, "B" * 64
    text = f"{h1}  spectra-backend-linux-x86_64.tar.gz\n{h2} *dir/other.zip\n\ngarbage line\nxyz  bad\n"
    sums = _parse_sha256sums(text)
    assert sums == {
        "spectra-backend-linux-x86_64.tar.gz": h1,
        "other.zip": h2.lower(),
    }


def test_extract_only_known_regular_files(tmp_path):
    elf = b"\x7fELF" + b"\x00" * 50
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        for name, data in (
            ("bin/spectra-backend", elf),
            ("../../tmp/evilfile", b"evil"),
            ("../malicious", b"bad"),
        ):
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
        # A symlink with an allowed name must never be followed/extracted
        link = tarfile.TarInfo(name="spectra-window")
        link.type = tarfile.SYMTYPE
        link.linkname = "/etc/passwd"
        tf.addfile(link)
    files = _extract_binaries(buf.getvalue(), "x.tar.gz", str(tmp_path))
    assert sorted(os.listdir(tmp_path)) == ["spectra-backend"]
    assert files == {"spectra-backend": hashlib.sha256(elf).hexdigest()}
    assert os.access(tmp_path / "spectra-backend", os.X_OK)


@pytest.mark.skipif(platform.system() == "Windows", reason="tar.gz assets on POSIX")
def test_download_verified_and_cached(cache_root, fake_release):
    assets, fetched = fake_release
    plat = _detect_platform()
    _, archive = _asset_url("9.9.9", plat)
    data = _tar_gz({"spectra-backend": b"\x7fELF backend", "spectra-window": b"\x7fELF win"})
    assets[archive] = data
    assets["SHA256SUMS"] = f"{hashlib.sha256(data).hexdigest()}  {archive}\n".encode()

    path = download_backend("9.9.9", quiet=True)
    assert path == os.path.join(str(cache_root), "9.9.9", "spectra-backend")
    assert os.path.isfile(os.path.join(os.path.dirname(path), "spectra-window"))
    assert all("/v9.9.9/" in u for u in fetched)
    assert find_cached_backend("9.9.9") == path
    # Cache is version-pinned: a different package version does not reuse it
    assert find_cached_backend("9.9.8") is None

    # Second call is served from cache without network
    fetched.clear()
    assert download_backend("9.9.9", quiet=True) == path
    assert fetched == []


@pytest.mark.skipif(platform.system() == "Windows", reason="tar.gz assets on POSIX")
def test_download_refuses_checksum_mismatch(cache_root, fake_release):
    assets, _ = fake_release
    _, archive = _asset_url("9.9.9", _detect_platform())
    assets[archive] = _tar_gz({"spectra-backend": b"\x7fELF tampered"})
    assets["SHA256SUMS"] = f"{'0' * 64}  {archive}\n".encode()

    with pytest.raises(ChecksumError, match="mismatch"):
        download_backend("9.9.9", quiet=True)
    assert find_cached_backend("9.9.9") is None
    assert not os.path.exists(os.path.join(str(cache_root), "9.9.9"))


def test_download_refuses_missing_checksum_entry(cache_root, fake_release):
    assets, _ = fake_release
    _, archive = _asset_url("9.9.9", _detect_platform())
    assets[archive] = b"whatever"
    assets["SHA256SUMS"] = f"{'1' * 64}  some-other-file.deb\n".encode()
    with pytest.raises(ChecksumError, match="no entry"):
        download_backend("9.9.9", quiet=True)
    assert find_cached_backend("9.9.9") is None


def test_download_refuses_without_sha256sums(cache_root, fake_release):
    assets, _ = fake_release
    _, archive = _asset_url("9.9.9", _detect_platform())
    assets[archive] = b"whatever"
    with pytest.raises(RuntimeError, match="SHA256SUMS"):
        download_backend("9.9.9", quiet=True)


@pytest.mark.skipif(platform.system() == "Windows", reason="tar.gz assets on POSIX")
def test_tampered_cache_is_ignored(cache_root, fake_release):
    assets, _ = fake_release
    _, archive = _asset_url("9.9.9", _detect_platform())
    data = _tar_gz({"spectra-backend": b"\x7fELF backend"})
    assets[archive] = data
    assets["SHA256SUMS"] = f"{hashlib.sha256(data).hexdigest()}  {archive}\n".encode()
    path = download_backend("9.9.9", quiet=True)

    with open(path, "ab") as f:
        f.write(b"injected")
    assert find_cached_backend("9.9.9") is None


def test_cache_without_manifest_is_ignored(cache_root):
    d = cache_root / "9.9.9"
    d.mkdir(parents=True)
    exe = d / "spectra-backend"
    exe.write_bytes(b"\x7fELF")
    exe.chmod(0o755)
    assert find_cached_backend("9.9.9") is None


def test_auto_download_is_opt_in(monkeypatch):
    monkeypatch.delenv("SPECTRA_AUTO_DOWNLOAD", raising=False)
    monkeypatch.delenv("SPECTRA_NO_DOWNLOAD", raising=False)
    assert dl.auto_download_enabled() is False
    monkeypatch.setenv("SPECTRA_AUTO_DOWNLOAD", "1")
    assert dl.auto_download_enabled() is True
    monkeypatch.setenv("SPECTRA_NO_DOWNLOAD", "1")
    assert dl.auto_download_enabled() is False
