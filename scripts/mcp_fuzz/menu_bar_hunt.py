"""Menu bar hunt helpers — discovery and expected side effects."""

from __future__ import annotations

from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from ui_action_log import StderrActionTracker

MENU_BAR_Y = 24
MENU_TOP_YS = (20, 24, 28, 32)
SUBMENU_Y_START = 76
SUBMENU_Y_STEP = 4
MENU_ROW_HEIGHT = 24
MENU_SEPARATOR_HEIGHT = 14
# Tab strip below command bar — avoid accidental tab-drag during sweeps.
TAB_BAR_Y_MIN = 44
TAB_BAR_Y_MAX = 92

# Skip items with native side effects or app exit in automation mode.
SKIP_MENU_LABELS = frozenset(
    {
        "Exit",
        "Export PNG",
        "Export SVG",
        "Copy as Image",
        "Save Workspace",
        "Load Workspace",
        "Save Figure...",
        "Load Figure...",
        "Screenshot (PNG)",
    }
)

# Menu label -> state effect key (see menu_effect_changed in button_hunt.py).
MENU_EFFECTS: dict[str, str] = {
    "New Figure": "figure_count",
    "Toggle Inspector": "panel:core.inspector",
    "Inspector": "panel:core.inspector",
    "Toggle Navigation Rail": "nav_rail_visible",
    "Toggle Timeline": "panel:core.timeline",
    "Timeline": "panel:core.timeline",
    "Toggle Curve Editor": "panel:core.curve_editor",
    "Curve Editor": "panel:core.curve_editor",
    "Toggle Topics": "panel:core.topics",
    "Topics": "panel:core.topics",
    "Plugins...": "panel:core.plugins",
    "Plugins": "panel:core.plugins",
    "Settings": "panel:core.settings",
    "Theme Settings": "theme_settings_visible",
}


def menu_item_y(items: list[dict], item_index: int) -> int:
    """Submenu row Y from list_menus items (accounts for separator rows in the popup)."""
    y = SUBMENU_Y_START
    for i in range(item_index):
        if items[i].get("separator"):
            y += MENU_SEPARATOR_HEIGHT
        else:
            y += MENU_ROW_HEIGHT
    return y


def is_tab_bar_region(x: int, y: int) -> bool:
    """True when coordinates fall on the figure tab strip (starts tab drag)."""
    return x > 72 and TAB_BAR_Y_MIN <= y <= TAB_BAR_Y_MAX


def menu_log_id(label: str) -> str:
    """Match C++ ui::log_ui_action id sanitization (spaces -> underscores)."""
    return label.replace(" ", "_").replace("\t", "_") if label else "-"


def skip_menu_label(label: str) -> bool:
    if label in SKIP_MENU_LABELS:
        return True
    if label.startswith("Export ") and "(." in label:
        return True
    return False


def first_actionable_label(items: list[dict]) -> str | None:
    for item in items:
        label = item.get("label", "")
        if label and not skip_menu_label(label):
            return label
    return None


def open_menu(
    menu_x: int,
    *,
    pump_fn: Callable[[int], None],
    click_fn: Callable[[int, int], None],
) -> None:
    click_fn(menu_x, MENU_BAR_Y)
    pump_fn(5)


def discover_top_menu_x(
    tracker: StderrActionTracker,
    probe_label: str,
    width: int,
    *,
    pump_fn: Callable[[int], None],
    click_fn: Callable[[int, int], None],
) -> int | None:
    """Find top-level menu X by probing until probe_label logs kind=menu."""
    probe_id = menu_log_id(probe_label)
    # Menus sit right of the nav rail (~72px) and app logo; File is typically x>=200.
    for x in range(200, min(width - 80, 420), 4):
        for y_sub in range(SUBMENU_Y_START, 280, SUBMENU_Y_STEP):
            open_menu(x, pump_fn=pump_fn, click_fn=click_fn)
            tracker.mark()
            click_fn(x, y_sub)
            pump_fn(4)
            if any(
                action["kind"] == "menu" and action["id"] == probe_id
                for action in tracker.new_actions()
            ):
                return x
    return None


def discover_menu_item_y(
    tracker: StderrActionTracker,
    menu_x: int,
    label: str,
    *,
    pump_fn: Callable[[int], None],
    click_fn: Callable[[int, int], None],
    max_y: int = 520,
) -> int | None:
    """Find submenu Y for one label (re-opens menu each attempt)."""
    target = menu_log_id(label)
    for y in range(SUBMENU_Y_START, max_y, SUBMENU_Y_STEP):
        open_menu(menu_x, pump_fn=pump_fn, click_fn=click_fn)
        tracker.mark()
        click_fn(menu_x, y)
        pump_fn(6)
        if any(
            action["kind"] == "menu" and action["id"] == target
            for action in tracker.new_actions()
        ):
            return y
    return None
