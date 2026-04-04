#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import OrderedDict
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]

WINDOW_MANAGER_FILES = [
    Path("src/ui/window/window_manager.cpp"),
    Path("src/ui/window/window_lifecycle.cpp"),
    Path("src/ui/window/window_figure_ops.cpp"),
    Path("src/ui/window/window_glfw_callbacks.cpp"),
]

KEY_FILES = OrderedDict(
    [
        ("src/ui/window/window_manager.cpp", "WindowManager orchestration"),
        ("src/ui/window/window_lifecycle.cpp", "Window lifecycle"),
        ("src/ui/window/window_figure_ops.cpp", "Window figure ops"),
        ("src/ui/window/window_glfw_callbacks.cpp", "Window GLFW callbacks"),
        ("src/ui/app/register_commands.cpp", "Command registration"),
        ("src/ui/automation/automation_server.cpp", "Automation server"),
        ("src/render/render_geometry.cpp", "Render geometry"),
        ("src/ui/imgui/imgui_integration.cpp", "ImGui integration"),
        ("src/ipc/codec.cpp", "Legacy IPC codec"),
        ("src/ipc/codec_fb.cpp", "FlatBuffers IPC codec"),
        ("src/render/webgpu/wgpu_backend.cpp", "WebGPU backend"),
        ("src/ui/app/window_ui_context.hpp", "WindowUIContext"),
    ]
)

WGSL_GLOB = "src/gpu/shaders/wgsl/*.wgsl"

WINDOW_MANAGER_METHOD_RE = re.compile(
    r"^(?:[\w:<>~*&\s]+?\s+)?WindowManager::([A-Za-z_~][A-Za-z0-9_]*)\s*\("
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def count_lines(path: Path) -> int:
    return len(read_text(path).splitlines())


def count_tree_lines(base: Path, suffixes: Iterable[str]) -> int:
    total = 0
    for path in sorted(base.rglob("*")):
        if path.is_file() and path.suffix in suffixes:
            total += count_lines(path)
    return total


def count_pattern(paths: Iterable[Path], pattern: re.Pattern[str]) -> int:
    total = 0
    for path in paths:
        text = read_text(path)
        total += len(pattern.findall(text))
    return total


def find_duplicates() -> dict[str, list[str]]:
    defs: dict[str, list[str]] = {}
    for rel_path in WINDOW_MANAGER_FILES:
        path = ROOT / rel_path
        if not path.exists():
            continue
        for line_no, line in enumerate(read_text(path).splitlines(), 1):
            match = WINDOW_MANAGER_METHOD_RE.match(line.strip())
            if not match:
                continue
            defs.setdefault(match.group(1), []).append(f"{rel_path}:{line_no}")
    duplicates: dict[str, list[str]] = {}
    for name, locs in sorted(defs.items()):
        file_set = {location.split(":", 1)[0] for location in locs}
        if len(file_set) > 1:
            duplicates[name] = locs
    return duplicates


def collect_metrics() -> dict[str, object]:
    src_files = [p for p in (ROOT / "src").rglob("*") if p.is_file()]
    include_files = [p for p in (ROOT / "include").rglob("*") if p.is_file()]

    key_file_lines = OrderedDict()
    for rel_path, label in KEY_FILES.items():
        path = ROOT / rel_path
        key_file_lines[rel_path] = {
            "label": label,
            "lines": count_lines(path) if path.exists() else None,
        }

    wgsl_files = sorted((ROOT / "src/gpu/shaders/wgsl").glob("*.wgsl"))
    duplicates = find_duplicates()

    return {
        "totals": {
            "src_cpp_lines": count_tree_lines(ROOT / "src", {".cpp", ".hpp", ".c", ".h"}),
            "tests_cpp_lines": count_tree_lines(ROOT / "tests", {".cpp", ".hpp", ".c", ".h"}),
            "python_lines": count_tree_lines(ROOT / "python", {".py"}),
        },
        "key_file_lines": key_file_lines,
        "wgsl": {
            "file_count": len(wgsl_files),
            "total_lines": sum(count_lines(path) for path in wgsl_files),
            "files": {str(path.relative_to(ROOT)): count_lines(path) for path in wgsl_files},
        },
        "theme_usage": {
            "ui_theme_calls": count_pattern(
                src_files + include_files, re.compile(r"\bui::theme\s*\(")
            ),
            "theme_manager_instance_calls": count_pattern(
                src_files + include_files, re.compile(r"ThemeManager::instance\s*\(")
            ),
        },
        "commands": {
            "register_command_calls": count_pattern(
                [ROOT / "src/ui/app/register_commands.cpp"], re.compile(r"\.register_command\s*\(")
            ),
        },
        "automation": {
            "method_dispatch_branches": count_pattern(
                [ROOT / "src/ui/automation/automation_server.cpp"],
                re.compile(r'if\s*\(\s*method\s*==\s*"'),
            ),
        },
        "window_manager_duplicates": duplicates,
    }


def render_markdown(metrics: dict[str, object]) -> str:
    key_file_lines = metrics["key_file_lines"]
    wgsl = metrics["wgsl"]
    theme_usage = metrics["theme_usage"]
    commands = metrics["commands"]
    automation = metrics["automation"]
    totals = metrics["totals"]
    duplicates = metrics["window_manager_duplicates"]

    lines: list[str] = []
    lines.append("# Architecture Metrics")
    lines.append("")
    lines.append("## Totals")
    lines.append("")
    lines.append(f"- `src/` C/C++ lines: {totals['src_cpp_lines']}")
    lines.append(f"- `tests/` C/C++ lines: {totals['tests_cpp_lines']}")
    lines.append(f"- `python/` lines: {totals['python_lines']}")
    lines.append("")
    lines.append("## Key Files")
    lines.append("")
    lines.append("| File | Lines | Notes |")
    lines.append("|---|---:|---|")
    for rel_path, item in key_file_lines.items():
        lines.append(f"| `{rel_path}` | {item['lines']} | {item['label']} |")
    lines.append("")
    lines.append("## Focus Metrics")
    lines.append("")
    lines.append(f"- `ui::theme()` call sites: {theme_usage['ui_theme_calls']}")
    lines.append(
        f"- `ThemeManager::instance()` call sites: {theme_usage['theme_manager_instance_calls']}"
    )
    lines.append(f"- `register_command()` calls: {commands['register_command_calls']}")
    lines.append(
        f"- `AutomationServer::execute()` method branches: {automation['method_dispatch_branches']}"
    )
    lines.append(
        f"- WGSL shader files: {wgsl['file_count']} ({wgsl['total_lines']} total lines)"
    )
    lines.append("")
    lines.append("## WindowManager Duplicate Definitions")
    lines.append("")
    if not duplicates:
        lines.append("- None")
    else:
        for method, locations in duplicates.items():
            joined = ", ".join(f"`{location}`" for location in locations)
            lines.append(f"- `{method}`: {joined}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect scripted architecture metrics.")
    parser.add_argument(
        "--format",
        choices=("json", "markdown"),
        default="markdown",
        help="Output format.",
    )
    parser.add_argument(
        "--fail-on-window-manager-duplicates",
        action="store_true",
        help="Exit non-zero when duplicated WindowManager method definitions exist.",
    )
    args = parser.parse_args()

    metrics = collect_metrics()

    if args.format == "json":
        print(json.dumps(metrics, indent=2))
    else:
        print(render_markdown(metrics))

    if args.fail_on_window_manager_duplicates and metrics["window_manager_duplicates"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
