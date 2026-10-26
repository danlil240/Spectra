#!/usr/bin/env python3
"""Tests for packaging metadata used by release wheel builds."""
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11
    import tomli as tomllib


ROOT = Path(__file__).resolve().parents[2]


def _requirement_name(requirement):
    name = requirement.split(";", 1)[0].split("[", 1)[0]
    for separator in ("==", ">=", "<=", "~=", "!=", ">", "<"):
        name = name.split(separator, 1)[0]
    return name.strip().lower().replace("_", "-")


def test_root_build_requirements_include_icon_generator_dependencies():
    icon_generator = ROOT / "cmake" / "generate_hicolor_icons.py"
    pyproject = tomllib.loads((ROOT / "pyproject.toml").read_text())

    assert "from PIL import Image" in icon_generator.read_text()

    requirements = pyproject["build-system"]["requires"]
    requirement_names = {_requirement_name(requirement) for requirement in requirements}
    assert "pillow" in requirement_names


def _pyproject():
    return tomllib.loads((ROOT / "pyproject.toml").read_text())


def test_single_python_project_definition():
    # The root pyproject.toml is the only packaging definition (no drift).
    assert not (ROOT / "python" / "pyproject.toml").exists()
    assert not (ROOT / "python" / "VERSION").exists()


def test_version_comes_from_version_txt():
    import re

    project = _pyproject()
    assert "version" in project["project"]["dynamic"]
    meta = project["tool"]["scikit-build"]["metadata"]["version"]
    assert meta["input"] == "version.txt"
    version_txt = (ROOT / "version.txt").read_text().strip()
    match = re.search(meta["regex"], version_txt)
    assert match and match.group("value") == version_txt


def test_runtime_dependencies_declared():
    deps = {_requirement_name(r) for r in _pyproject()["project"]["dependencies"]}
    # spectra/_codec_fb.py imports flatbuffers unconditionally
    assert "flatbuffers" in deps


def test_wheel_installs_only_python_component():
    skb = _pyproject()["tool"]["scikit-build"]
    assert skb["install"]["components"] == ["python_wheel"]
    assert "install-dir" not in skb.get("wheel", {})
    cmake = (ROOT / "CMakeLists.txt").read_text()
    assert "COMPONENT python_wheel" in cmake
    wheel_block = cmake[cmake.index("foreach(_spectra_wheel_bin"):]
    wheel_block = wheel_block[: wheel_block.index("endforeach()")]
    assert "spectra-backend" in wheel_block and "spectra-window" in wheel_block
    assert "DESTINATION spectra/_bin" in wheel_block
