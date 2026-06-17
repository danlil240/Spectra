"""Nav rail button layout — mirrors SpectraNavRail draw order and spacing."""

from __future__ import annotations

from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from ui_action_log import StderrActionTracker

# C++ constants from layout_manager.hpp / design_tokens.hpp
COMMAND_BAR_HEIGHT = 48.0
STATUS_BAR_HEIGHT = 24.0
NAV_RAIL_COLLAPSED_WIDTH = 72.0
NAV_RAIL_CELL_HEIGHT = 56.0
NAV_RAIL_CELL_HEIGHT_MIN = 36.0
NAV_RAIL_SEPARATOR_HEIGHT = 9.0
NAV_RAIL_TOP_PADDING = 12.0  # ui::tokens::SPACE_3

# Groups match SpectraNavRail::build_items (section headers add a separator each)
NAV_RAIL_GROUPS: tuple[tuple[tuple[str, str | None], ...], ...] = (
    (
        ("Select", "interaction_mode:Select"),
        ("Pan", "interaction_mode:Pan"),
        ("Zoom", "interaction_mode:Zoom"),
    ),
    (
        ("Measure", "interaction_mode:Measure"),
        ("Annotate", "interaction_mode:Annotate"),
        ("ROI", "interaction_mode:ROI"),
    ),
    (
        ("Markers", None),
        ("Transform", "transform_dialog_open:true"),
    ),
    (
        ("Inspector", "panel:core.inspector"),
        ("Timeline", "panel:core.timeline"),
        ("Curve Editor", "panel:core.curve_editor"),
        ("Plugins", "panel:core.plugins"),
        ("Topics", "panel:core.topics"),
        ("Settings", "panel:core.settings"),
    ),
    (("Help", None),),
)

BUTTON_COUNT = sum(len(group) for group in NAV_RAIL_GROUPS)
SEPARATOR_COUNT = len(NAV_RAIL_GROUPS) - 1 + 1  # between groups + Panels category header


def sanitize_id(label: str) -> str:
    return label.replace(" ", "_").replace("\t", "_") if label else "-"


def nav_rail_scale(window_height: int) -> float:
    """Match LayoutManager::nav_rail_scale_for_height."""
    content_h = max(0.0, window_height - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT)
    nominal = (
        NAV_RAIL_TOP_PADDING
        + BUTTON_COUNT * NAV_RAIL_CELL_HEIGHT
        + SEPARATOR_COUNT * NAV_RAIL_SEPARATOR_HEIGHT
    )
    min_h = (
        NAV_RAIL_TOP_PADDING
        + BUTTON_COUNT * NAV_RAIL_CELL_HEIGHT_MIN
        + SEPARATOR_COUNT * NAV_RAIL_SEPARATOR_HEIGHT
    )
    if content_h >= nominal:
        return 1.0
    if content_h <= min_h:
        return NAV_RAIL_CELL_HEIGHT_MIN / NAV_RAIL_CELL_HEIGHT
    return max(NAV_RAIL_CELL_HEIGHT_MIN / NAV_RAIL_CELL_HEIGHT, content_h / nominal)


def discover_nav_rail_centers(
    tracker: StderrActionTracker,
    window_height: int,
    *,
    pump_fn: Callable[[int], None],
    click_fn: Callable[[int, int], None],
) -> dict[str, int]:
    """Y-sweep at x=36; record first hit per nav_rail log id."""
    centers: dict[str, int] = {}
    y_min = int(COMMAND_BAR_HEIGHT + 16)
    y_max = int(window_height - STATUS_BAR_HEIGHT - 8)
    for y in range(y_min, y_max, 8):
        tracker.mark()
        click_fn(36, y)
        pump_fn(4)
        for action in tracker.new_actions():
            if action["kind"] != "nav_rail":
                continue
            centers.setdefault(action["id"], y)
    return centers


def expected_buttons() -> list[tuple[str, str | None]]:
    out: list[tuple[str, str | None]] = []
    for group in NAV_RAIL_GROUPS:
        out.extend(group)
    return out


def _layout_estimated_positions(window_height: int) -> list[tuple[str, str | None, int]]:
    scale = nav_rail_scale(window_height)
    cell_h = NAV_RAIL_CELL_HEIGHT * scale
    sep_h = NAV_RAIL_SEPARATOR_HEIGHT * scale
    rail_bottom = window_height - STATUS_BAR_HEIGHT
    y = COMMAND_BAR_HEIGHT + NAV_RAIL_TOP_PADDING * scale

    out: list[tuple[str, str | None, int]] = []
    for group_index, group in enumerate(NAV_RAIL_GROUPS):
        if group_index > 0:
            y += sep_h
        for label, expect in group:
            y_center = int(min(y + cell_h * 0.5, rail_bottom - 4))
            out.append((label, expect, y_center))
            y += cell_h
    return out


def nav_rail_click_positions(
    window_height: int,
    discovered: dict[str, int] | None = None,
) -> list[tuple[str, int, int, str | None]]:
    """Return (label, x, y, expect) using live discovery map when provided."""
    x = int(NAV_RAIL_COLLAPSED_WIDTH * 0.5)
    estimates = {label: y for label, _exp, y in _layout_estimated_positions(window_height)}
    out: list[tuple[str, int, int, str | None]] = []
    for label, expect in expected_buttons():
        key = sanitize_id(label)
        if discovered is not None:
            if key not in discovered:
                continue
            y = discovered[key]
        else:
            y = estimates[label]
        out.append((label, x, y, expect))
    return out
