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
