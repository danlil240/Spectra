#!/usr/bin/env python3
"""Fail on hard-coded ImGui colour literals in UI code.

UI colours must come from ThemeManager semantic tokens (ui::theme().text_primary,
.warning, .error, .accent, ...; see src/ui/theme/theme_imgui.hpp for the
to_imvec4()/to_imu32() helpers) so panels stay readable in every theme
(dark, night, light, high_contrast).

Flagged in src/ui and src/adapters:
    IM_COL32(<number>, ...)
    ImVec4(<number>, ...)
    ImVec4 name(<number>, ...)   /   ImVec4 name{<number>, ...}

Not flagged: pure black with any alpha (IM_COL32(0, 0, 0, a), ImVec4(0, 0, 0, 0)),
i.e. "transparent" and black drop shadows, which read the same on every theme.

Legitimate exceptions (theme definitions, colormaps, decorative effects, 3D
scene colours) live in tools/color_literals_allowlist.txt, one per line:
    path/to/file.cpp                       # whole file
    path/to/file.cpp: <source line>        # one line, compared after stripping
Lines starting with '#' are comments.

Usage: tools/check_color_literals.py [--root DIR] [--allowlist FILE] [--list]
  --list  print every finding in allowlist format and exit 0 (to seed the file).
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

SCAN_DIRS = ("src/ui", "src/adapters")
EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".inl"}

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[fF]?"
PATTERNS = [
    re.compile(r"\bIM_COL32\s*\(\s*(?=" + NUM + r")"),
    re.compile(r"\bImVec4\s*\(\s*(?=" + NUM + r")"),
    re.compile(r"\bImVec4\s+\w+\s*[({]\s*(?=" + NUM + r")"),
]
ZERO = re.compile(r"^[-+]?0*\.?0*[fF]?$")


def split_args(text: str, start: int) -> list[str]:
    """Split the comma-separated arguments that begin at text[start]."""
    args, depth, cur = [], 0, []
    for ch in text[start:]:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    args.append("".join(cur).strip())
    return args


def is_pure_black(args: list[str]) -> bool:
    return len(args) >= 3 and all(ZERO.match(a) for a in args[:3])


def strip_comments(line: str) -> str:
    return line.split("//", 1)[0]


def find_literals(root: Path):
    for scan_dir in SCAN_DIRS:
        base = root / scan_dir
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            for lineno, line in enumerate(lines, 1):
                code = strip_comments(line)
                for pat in PATTERNS:
                    hit = False
                    for m in pat.finditer(code):
                        if not is_pure_black(split_args(code, m.end())):
                            hit = True
                            break
                    if hit:
                        yield rel, lineno, line.strip()
                        break


def load_allowlist(path: Path):
    files, lines = set(), set()
    if not path.is_file():
        return files, lines
    for raw in path.read_text(encoding="utf-8").splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        if ": " in entry:
            fpath, src = entry.split(": ", 1)
            lines.add((fpath.strip(), src.strip()))
        else:
            files.add(entry)
    return files, lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", default=Path(__file__).resolve().parent.parent, type=Path)
    parser.add_argument("--allowlist", type=Path, default=None)
    parser.add_argument("--list", action="store_true")
    opts = parser.parse_args()

    root = opts.root.resolve()
    allowlist = opts.allowlist or root / "tools" / "color_literals_allowlist.txt"
    allowed_files, allowed_lines = load_allowlist(allowlist)

    findings = list(find_literals(root))
    if opts.list:
        for rel, _, src in findings:
            print(f"{rel}: {src}")
        return 0

    used = set()
    bad = []
    for rel, lineno, src in findings:
        if rel in allowed_files:
            continue
        if (rel, src) in allowed_lines:
            used.add((rel, src))
            continue
        bad.append((rel, lineno, src))

    for stale in sorted(allowed_lines - used):
        print(f"note: stale allowlist entry (no longer matches): {stale[0]}: {stale[1]}")

    if bad:
        for rel, lineno, src in bad:
            print(f"{rel}:{lineno}: hard-coded colour literal: {src}")
            if "GITHUB_ACTIONS" in os.environ:
                print(f"::error file={rel},line={lineno}::Hard-coded colour literal; "
                      "use a ui::theme() token (see src/ui/theme/theme_imgui.hpp)")
        print(
            f"\n{len(bad)} hard-coded colour literal(s) in UI code.\n"
            "Use a ThemeManager token instead, e.g. ui::to_imvec4(ui::theme().warning)\n"
            "(src/ui/theme/theme_imgui.hpp). If the literal is genuinely theme-independent\n"
            f"(colormap, 3D scene colour, decorative effect), add it to {allowlist.relative_to(root)}\n"
            "with a comment explaining why.")
        return 1
    print(f"OK: no unapproved colour literals in {', '.join(SCAN_DIRS)} "
          f"({len(findings)} allowlisted).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
