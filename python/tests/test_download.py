#!/usr/bin/env python3
"""Quick test for _download module logic."""
import io
import os
import sys
import tarfile
import tempfile
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "python"))

from spectra._download import (
    _detect_platform,
    _cache_dir,
    _asset_url,
    _read_version,
    _download_and_extract,
    find_cached_backend,
)
import spectra._download as dl


def test_platform_detection():
    plat = _detect_platform()
    assert plat == "linux-x86_64"


def test_cache_directory():
    cache = _cache_dir()
    assert "spectra" in cache and "bin" in cache


def test_asset_url_linux():
    url, name = _asset_url("0.2.1", "linux-x86_64")
    assert url == "https://github.com/danlil240/Spectra/releases/download/v0.2.1/spectra-backend-linux-x86_64.tar.gz"
    assert name == "spectra-backend-linux-x86_64.tar.gz"


def test_asset_url_windows():
    url_w, name_w = _asset_url("0.2.1", "windows-x86_64")
    assert url_w.endswith(".zip")
    assert name_w.endswith(".zip")


class FakeResp:
    def __init__(self, data):
        self._data = data
    def read(self):
        return self._data


def test_tar_extraction():
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        for n in ("bin/spectra-backend", "bin/spectra-window"):
            info = tarfile.TarInfo(name=n)
            data = b"\x7fELF" + b"\x00" * 100
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
    buf.seek(0)
    tmpdir = tempfile.mkdtemp()
    orig_urlopen = dl.urlopen

    dl.urlopen = lambda req, timeout=60: FakeResp(buf.read())
    try:
        dl._download_and_extract("http://fake/test.tar.gz", tmpdir)
        assert os.path.isfile(os.path.join(tmpdir, "spectra-backend"))
        assert os.path.isfile(os.path.join(tmpdir, "spectra-window"))
        assert os.access(os.path.join(tmpdir, "spectra-backend"), os.X_OK)
    finally:
        dl.urlopen = orig_urlopen
        shutil.rmtree(tmpdir)


def test_path_traversal_protection():
    """Verify only allowed binaries are extracted, regardless of tar member names."""
    buf2 = io.BytesIO()
    with tarfile.open(fileobj=buf2, mode="w:gz") as tf:
        info = tarfile.TarInfo(name="spectra-backend")
        data = b"\x7fELF" + b"\x00" * 50
        info.size = len(data)
        tf.addfile(info, io.BytesIO(data))
        # Malicious entry — basename "evilfile" is not in the allowed list
        info2 = tarfile.TarInfo(name="../../tmp/evilfile")
        data2 = b"evil"
        info2.size = len(data2)
        tf.addfile(info2, io.BytesIO(data2))
        # Another malicious entry with an allowed-sounding directory
        info3 = tarfile.TarInfo(name="../malicious")
        data3 = b"bad"
        info3.size = len(data3)
        tf.addfile(info3, io.BytesIO(data3))
    buf2.seek(0)
    tmpdir2 = tempfile.mkdtemp()
    orig_urlopen = dl.urlopen
    dl.urlopen = lambda req, timeout=60: FakeResp(buf2.read())
    try:
        dl._download_and_extract("http://fake/test.tar.gz", tmpdir2)
        assert os.path.isfile(os.path.join(tmpdir2, "spectra-backend"))
        # Only spectra-backend should exist — nothing else
        extracted = os.listdir(tmpdir2)
        assert sorted(extracted) == ["spectra-backend"], f"Unexpected files: {extracted}"
    finally:
        dl.urlopen = orig_urlopen
        shutil.rmtree(tmpdir2)


def test_version_reading():
    ver = _read_version()
    assert ver, "Version should not be empty"
