#!/usr/bin/env python3
"""One-shot optimizer: migrate skills/ content in .cursor/skills/ to Cursor conventions."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKILLS = ROOT / ".cursor" / "skills"

DESCRIPTIONS: dict[str, str] = {
    "code-simplifier": (
        "Simplifies Spectra C++ while preserving behavior, Vulkan sync safety, "
        "threading, and public APIs. Use for deduplication, flattening control flow, "
        "narrowing interfaces, dead-code removal, or when the user asks to simplify "
        "or reduce complexity — not for rewrites, renames-only, or swapchain/fence changes."
    ),
    "3d-rendering": (
        "Develops Spectra 3D rendering: Axes3D, Series3D, camera, depth, colormaps, "
        "Blinn-Phong lighting, and 3D shaders. Use for mesh/surface/point cloud work, "
        "3D visual bugs, camera orbit/pan/zoom, or anything under axes3d/ and *3d shaders."
    ),
    "python-bindings": (
        "Extends or debugs the Spectra Python package and IPC client in python/spectra/. "
        "Use for plot()/show() easy API, session/transport errors, codec drift vs C++, "
        "pyproject, examples, or embedding — whenever Python talks to the backend daemon."
    ),
    "ipc-protocol-dev": (
        "Modifies Spectra binary IPC: message types, TLV codec, daemon routing, transport. "
        "Use for multi-process mode, REQ_* messages, Python↔C++ codec parity, session graph, "
        "or framing/desync bugs in src/ipc/ and src/daemon/."
    ),
    "data-pipeline": (
        "Works on Spectra data path: decimation (LTTB), filters, transforms, streaming "
        "append, CSV load, GPU upload. Use for large datasets, live data, NaN handling, "
        "bench_decimation, or src/data/ and series append performance."
    ),
    "graphical-change-workflow": (
        "Mandatory visual validation loop for Spectra rendering/UI changes: build, kill "
        "old processes, launch app, plot via MCP, screenshot, golden compare. Use for ANY "
        "shader, theme, ImGui, or pixel change — even small color tweaks."
    ),
    "build-system": (
        "Changes Spectra CMake: SPECTRA_SOURCES, feature flags, examples, tests, shaders, "
        "FetchContent. Use for link errors, new targets, CI CMake, CompileShaders.cmake, or "
        "adding files to the build."
    ),
    "qa-designer-agent": (
        "Runs Spectra visual QA with spectra_qa_agent --design-review: 57 screenshots, "
        "triage P0–P3 in plans/QA_design_review.md, minimal theme/ImGui fixes. Use for "
        "UI polish, visual bugs, design review, or screenshot regressions."
    ),
    "qa-performance-agent": (
        "Runs Spectra stress/fuzz QA with spectra_qa_agent, triages qa_report, reproduces "
        "seed crashes, updates plans/QA_results.md. Use for stability, performance regressions, "
        "fuzz failures, or long-running QA sessions."
    ),
    "qa-regression-agent": (
        "Guards Spectra pixel output via golden tests and unit suite. Use when golden fails, "
        "updating baselines, adding golden coverage for new series/UI, or before merge on "
        "rendering changes."
    ),
    "qa-api-agent": (
        "Tests Spectra Python API, IPC roundtrips, and C++ easy API contracts. Use when "
        "pytest fails, cross_codec breaks, examples crash, or public API compatibility is "
        "at risk."
    ),
    "qa-accessibility-agent": (
        "Audits Spectra accessibility: WCAG contrast, colorblind palettes, keyboard nav, "
        "high-contrast theme. Use when changing theme colors, legend/series colors, or new "
        "interactive UI controls."
    ),
    "qa-memory-agent": (
        "Hunts Spectra CPU/GPU memory leaks with ASan, Valgrind, VMA budget. Use for RSS "
        "growth, figure close not freeing GPU memory, or open issue M1 in QA docs."
    ),
    "qa-ros-performance-agent": (
        "QA for spectra-ros: spectra_ros_qa_agent scenarios, ROS UI/dataflow regressions. "
        "Use when ROS2 adapter, bag player, or ros panels break under stress."
    ),
}

MCP_SECTION_RE = re.compile(
    r"\n## Live (?:Smoke Test|Validation|Verification).*?via MCP Server\n.*?(?=\n## |\n---\n\n## |\Z)",
    re.DOTALL,
)

MCP_SERVER_SECTION_RE = re.compile(
    r"\n## Spectra MCP Server\n.*?(?=\n## |\Z)",
    re.DOTALL,
)

QA_TAIL_SECTIONS_RE = re.compile(
    r"\n## (?:Mandatory Session Self-Improvement|Self-Update Protocol)\n.*?(?=\n## |\Z)",
    re.DOTALL,
)

MCP_REPLACE = (
    "\n## Live validation\n\n"
    "After build: [spectra-mcp](../spectra-mcp/SKILL.md) (`ping`, plot, screenshot). "
    "Full loop: [graphical-change-workflow](../graphical-change-workflow/SKILL.md).\n"
)

PATH_REPLACEMENTS = [
    ("skills/vulkan-shader-dev/SKILL.md", "../add-shader/SKILL.md"),
    ("skills/ipc-protocol-dev/SKILL.md", "../ipc-protocol-dev/SKILL.md"),
    ("See `skills/graphical-change-workflow/SKILL.md`", "See graphical-change-workflow skill"),
    (".windsurf/skills/spectra-skills/SKILL.md", "../spectra-mcp/SKILL.md"),
]
# Only rewrite skills/qa-* and skills/code-simplifier/ when not already under .cursor/


def patch_frontmatter(text: str, name: str) -> str:
    if name not in DESCRIPTIONS:
        return text
    desc = DESCRIPTIONS[name]
    if not text.startswith("---"):
        return text
    end = text.index("---", 3)
    body = text[end + 3 :]
    return f"---\nname: {name}\ndescription: >-\n  {desc}\n---{body}"


def trim_qa_designer(path: Path) -> None:
    text = path.read_text()
    marker = "\n## Design-Review Screenshot Coverage"
    if marker not in text:
        return
    head, tail = text.split(marker, 1)
    ref = path.parent / "references" / "qa-designer-reference.md"
    appendix = f"\n\n---\n\n# Extended reference (from SKILL)\n{marker}{tail}"
    if marker not in ref.read_text():
        ref.write_text(ref.read_text().rstrip() + appendix)
    footer = (
        "\n## Extended reference\n\n"
        "- Screenshot coverage (57), command reference, issue maps, MCP, guardrails: "
        "[references/qa-designer-reference.md](references/qa-designer-reference.md)\n"
        "- Session report template: [REPORT.md](REPORT.md)\n"
    )
    path.write_text(head.rstrip() + footer)


def process_skill_md(path: Path) -> None:
    name = path.parent.name
    if name in ("spectra-index", "spectra-mcp", "build-and-test", "debug-vulkan",
                "add-command", "add-test", "add-example", "add-series-type", "add-shader"):
        return
    text = path.read_text()
    text = patch_frontmatter(text, name)
    for old, new in PATH_REPLACEMENTS:
        text = text.replace(old, new)
    if name != "spectra-mcp" and "graphical-change-workflow" not in name:
        text = MCP_SECTION_RE.sub(MCP_REPLACE, text)
    if name.startswith("qa-"):
        text = MCP_SERVER_SECTION_RE.sub(
            "\n## MCP\n\n[spectra-mcp](../spectra-mcp/SKILL.md).\n", text
        )
        text = QA_TAIL_SECTIONS_RE.sub(
            "\n## Session notes\n\nUpdate [REPORT.md](REPORT.md) and "
            "`plans/QA_*.md`; extend `references/` if you discover new patterns.\n",
            text,
        )
        ref_link = ""
        ref_dir = path.parent / "references"
        if ref_dir.is_dir() and list(ref_dir.glob("*.md")):
            ref_name = next(iter(sorted(ref_dir.glob("*.md")))).name
            ref_link = f"\n## Reference\n\nDetailed tables: [references/{ref_name}](references/{ref_name}).\n"
        if "## Reference\n" not in text and ref_link:
            text = text.rstrip() + ref_link
    # Remove applyTo (Windsurf-specific) from graphical-change-workflow
    text = re.sub(r"\napplyTo:.*\n", "\n", text)
    path.write_text(text)
    if name == "qa-designer-agent":
        trim_qa_designer(path)


def main() -> None:
    for skill_md in sorted(SKILLS.glob("*/SKILL.md")):
        process_skill_md(skill_md)
    print(f"Optimized {len(list(SKILLS.glob('*/SKILL.md')))} skills under {SKILLS}")


if __name__ == "__main__":
    main()
