#!/usr/bin/env python3
"""
Generate a minimal icon font for Spectra UI.

Creates a TTF font with vector-drawn icons mapped to Private Use Area
codepoints (0xE001-0xE063) matching the Icon enum in icons.hpp.

Each icon is drawn as simple vector paths in a 1000-unit em square.
The font is then compressed using ImGui's compression format and
output as a C++ header for embedding.
"""

from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


UNITS_PER_EM = 1000
ASCENT = 800
DESCENT = -200


def cubic_approx(pen, p0, p1, p2, p3, steps=8):
    """Approximate a cubic bezier with line segments (TTF doesn't support cubics)."""
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        x = int(u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0])
        y = int(u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1])
        pen.lineTo((x, y))


# Icon definitions: codepoint -> (name, draw_function)
# Each draw function takes a pen and draws paths in 0-1000 unit space

def draw_chart_line(pen):
    """📈 Line chart - rising line with grid"""
    pen.moveTo((100, 100))
    pen.lineTo((100, 900))
    pen.lineTo((900, 900))
    pen.lineTo((900, 840))
    pen.lineTo((160, 840))
    pen.lineTo((160, 100))
    pen.closePath()
    # Rising line
    pen.moveTo((200, 700))
    pen.lineTo((400, 400))
    pen.lineTo((600, 550))
    pen.lineTo((850, 200))
    pen.lineTo((850, 300))
    pen.lineTo((600, 650))
    pen.lineTo((400, 500))
    pen.lineTo((200, 800))
    pen.closePath()

def draw_scatter_chart(pen):
    """📊 Bar chart"""
    # Axes
    pen.moveTo((100, 100))
    pen.lineTo((100, 900))
    pen.lineTo((160, 900))
    pen.lineTo((160, 160))
    pen.lineTo((900, 160))
    pen.lineTo((900, 100))
    pen.closePath()
    # Bar 1
    pen.moveTo((220, 160))
    pen.lineTo((220, 500))
    pen.lineTo((370, 500))
    pen.lineTo((370, 160))
    pen.closePath()
    # Bar 2
    pen.moveTo((420, 160))
    pen.lineTo((420, 750))
    pen.lineTo((570, 750))
    pen.lineTo((570, 160))
    pen.closePath()
    # Bar 3
    pen.moveTo((620, 160))
    pen.lineTo((620, 600))
    pen.lineTo((770, 600))
    pen.lineTo((770, 160))
    pen.closePath()

def draw_axes(pen):
    """⊞ Axes / crosshair grid"""
    # Horizontal bar
    pen.moveTo((50, 460))
    pen.lineTo((50, 540))
    pen.lineTo((950, 540))
    pen.lineTo((950, 460))
    pen.closePath()
    # Vertical bar
    pen.moveTo((460, 50))
    pen.lineTo((460, 950))
    pen.lineTo((540, 950))
    pen.lineTo((540, 50))
    pen.closePath()

def draw_wrench(pen):
    """🔧 Wrench / tools"""
    pen.moveTo((200, 100))
    pen.lineTo((450, 350))
    pen.lineTo((350, 450))
    pen.lineTo((100, 200))
    pen.closePath()
    pen.moveTo((550, 550))
    pen.lineTo((900, 900))
    pen.lineTo((850, 950))
    pen.lineTo((500, 600))
    pen.closePath()
    # Circle head
    _draw_filled_circle(pen, 720, 580, 120)

def draw_folder(pen):
    """📁 Folder"""
    pen.moveTo((100, 200))
    pen.lineTo((100, 800))
    pen.lineTo((900, 800))
    pen.lineTo((900, 350))
    pen.lineTo((500, 350))
    pen.lineTo((420, 200))
    pen.closePath()
    # Inner
    pen.moveTo((160, 740))
    pen.lineTo((840, 740))
    pen.lineTo((840, 410))
    pen.lineTo((160, 410))
    pen.closePath()

def draw_settings(pen):
    """⚙️ Gear"""
    import math
    cx, cy = 500, 500
    r_outer = 420
    r_inner = 280
    r_hole = 150
    teeth = 8
    points_outer = []
    points_inner = []
    for i in range(teeth * 2):
        angle = math.pi * 2 * i / (teeth * 2) - math.pi / 2
        r = r_outer if i % 2 == 0 else r_inner
        x = cx + r * math.cos(angle)
        y = cy + r * math.sin(angle)
        if i % 2 == 0:
            points_outer.append((x, y))
        else:
            points_inner.append((x, y))
    # Outer gear shape
    all_pts = []
    for i in range(teeth):
        all_pts.append(points_outer[i])
        all_pts.append(points_inner[i])
    pen.moveTo((int(all_pts[0][0]), int(all_pts[0][1])))
    for pt in all_pts[1:]:
        pen.lineTo((int(pt[0]), int(pt[1])))
    pen.closePath()
    # Center hole (counter-clockwise for cutout)
    steps = 16
    hole_pts = []
    for i in range(steps):
        angle = -math.pi * 2 * i / steps
        x = cx + r_hole * math.cos(angle)
        y = cy + r_hole * math.sin(angle)
        hole_pts.append((int(x), int(y)))
    pen.moveTo(hole_pts[0])
    for pt in hole_pts[1:]:
        pen.lineTo(pt)
    pen.closePath()

def draw_help(pen):
    """❓ Question mark circle"""
    _draw_circle(pen, 500, 500, 450, 380)
    # Question mark - simplified with lines
    pen.moveTo((430, 520))
    pen.lineTo((570, 520))
    pen.lineTo((570, 580))
    pen.lineTo((530, 620))
    pen.lineTo((530, 660))
    pen.lineTo((470, 660))
    pen.lineTo((470, 600))
    pen.lineTo((420, 560))
    pen.lineTo((420, 700))
    pen.lineTo((580, 700))
    pen.lineTo((580, 560))
    pen.lineTo((500, 520))
    pen.closePath()
    # Dot
    pen.moveTo((460, 350))
    pen.lineTo((540, 350))
    pen.lineTo((540, 430))
    pen.lineTo((460, 430))
    pen.closePath()

def draw_zoom_in(pen):
    """🔍 Magnifying glass with +"""
    # Lens circle
    _draw_circle(pen, 420, 580, 320, 250)
    # Handle
    pen.moveTo((620, 340))
    pen.lineTo((900, 60))
    pen.lineTo((940, 100))
    pen.lineTo((660, 380))
    pen.closePath()
    # Plus horizontal
    pen.moveTo((300, 550))
    pen.lineTo((540, 550))
    pen.lineTo((540, 610))
    pen.lineTo((300, 610))
    pen.closePath()
    # Plus vertical
    pen.moveTo((390, 460))
    pen.lineTo((450, 460))
    pen.lineTo((450, 700))
    pen.lineTo((390, 700))
    pen.closePath()

def draw_hand(pen):
    """✋ Hand / pan"""
    # Fingers
    for x in [220, 370, 520, 670]:
        pen.moveTo((x, 300))
        pen.lineTo((x, 850))
        pen.lineTo((x + 80, 850))
        pen.lineTo((x + 80, 300))
        pen.closePath()
    # Palm base
    pen.moveTo((200, 150))
    pen.lineTo((770, 150))
    pen.lineTo((770, 350))
    pen.lineTo((200, 350))
    pen.closePath()

def draw_ruler(pen):
    """📏 Ruler"""
    pen.moveTo((100, 300))
    pen.lineTo((100, 700))
    pen.lineTo((900, 700))
    pen.lineTo((900, 300))
    pen.closePath()
    # Tick marks
    for i in range(9):
        x = 150 + i * 90
        h = 550 if i % 2 == 0 else 500
        pen.moveTo((x, 300))
        pen.lineTo((x, h))
        pen.lineTo((x + 30, h))
        pen.lineTo((x + 30, 300))
        pen.closePath()

def draw_crosshair(pen):
    """✛ Crosshair"""
    t = 30  # thickness
    # Horizontal
    pen.moveTo((50, 500 - t))
    pen.lineTo((950, 500 - t))
    pen.lineTo((950, 500 + t))
    pen.lineTo((50, 500 + t))
    pen.closePath()
    # Vertical
    pen.moveTo((500 - t, 50))
    pen.lineTo((500 + t, 50))
    pen.lineTo((500 + t, 950))
    pen.lineTo((500 - t, 950))
    pen.closePath()
    # Center circle
    _draw_circle(pen, 500, 500, 120, 60)

def draw_pin(pen):
    """📌 Pin / marker"""
    # Pin head (circle)
    _draw_circle(pen, 500, 650, 250, 180)
    # Pin point
    pen.moveTo((430, 420))
    pen.lineTo((500, 100))
    pen.lineTo((570, 420))
    pen.closePath()

def draw_type(pen):
    """T Type / text"""
    # Top bar
    pen.moveTo((150, 800))
    pen.lineTo((850, 800))
    pen.lineTo((850, 720))
    pen.lineTo((150, 720))
    pen.closePath()
    # Vertical stem
    pen.moveTo((460, 720))
    pen.lineTo((540, 720))
    pen.lineTo((540, 150))
    pen.lineTo((460, 150))
    pen.closePath()
    # Bottom serif
    pen.moveTo((350, 150))
    pen.lineTo((650, 150))
    pen.lineTo((650, 200))
    pen.lineTo((350, 200))
    pen.closePath()

def draw_export(pen):
    """📤 Export / upload arrow"""
    # Arrow up
    pen.moveTo((500, 900))
    pen.lineTo((750, 600))
    pen.lineTo((580, 600))
    pen.lineTo((580, 350))
    pen.lineTo((420, 350))
    pen.lineTo((420, 600))
    pen.lineTo((250, 600))
    pen.closePath()
    # Tray
    pen.moveTo((150, 300))
    pen.lineTo((150, 100))
    pen.lineTo((850, 100))
    pen.lineTo((850, 300))
    pen.lineTo((780, 300))
    pen.lineTo((780, 170))
    pen.lineTo((220, 170))
    pen.lineTo((220, 300))
    pen.closePath()

def draw_save(pen):
    """💾 Floppy disk / save"""
    # Outer
    pen.moveTo((100, 100))
    pen.lineTo((100, 900))
    pen.lineTo((900, 900))
    pen.lineTo((900, 250))
    pen.lineTo((750, 100))
    pen.closePath()
    # Inner cutout (top slot)
    pen.moveTo((300, 900))
    pen.lineTo((300, 650))
    pen.lineTo((700, 650))
    pen.lineTo((700, 900))
    pen.closePath()
    # Label area
    pen.moveTo((250, 500))
    pen.lineTo((750, 500))
    pen.lineTo((750, 200))
    pen.lineTo((250, 200))
    pen.closePath()

def draw_copy(pen):
    """📋 Copy"""
    # Back doc
    pen.moveTo((200, 200))
    pen.lineTo((200, 900))
    pen.lineTo((700, 900))
    pen.lineTo((700, 200))
    pen.closePath()
    pen.moveTo((260, 840))
    pen.lineTo((640, 840))
    pen.lineTo((640, 260))
    pen.lineTo((260, 260))
    pen.closePath()
    # Front doc
    pen.moveTo((300, 100))
    pen.lineTo((300, 750))
    pen.lineTo((800, 750))
    pen.lineTo((800, 100))
    pen.closePath()
    pen.moveTo((360, 690))
    pen.lineTo((740, 690))
    pen.lineTo((740, 160))
    pen.lineTo((360, 160))
    pen.closePath()

def draw_undo(pen):
    """↩ Undo arrow"""
    pen.moveTo((150, 550))
    pen.lineTo((400, 750))
    pen.lineTo((400, 650))
    pen.lineTo((600, 650))
    pen.lineTo((750, 600))
    pen.lineTo((800, 450))
    pen.lineTo((750, 300))
    pen.lineTo((820, 250))
    pen.lineTo((880, 420))
    pen.lineTo((870, 620))
    pen.lineTo((750, 720))
    pen.lineTo((600, 720))
    pen.lineTo((400, 720))
    pen.closePath()

def draw_redo(pen):
    """↪ Redo arrow"""
    pen.moveTo((850, 550))
    pen.lineTo((600, 750))
    pen.lineTo((600, 650))
    pen.lineTo((400, 650))
    pen.lineTo((250, 600))
    pen.lineTo((200, 450))
    pen.lineTo((250, 300))
    pen.lineTo((180, 250))
    pen.lineTo((120, 420))
    pen.lineTo((130, 620))
    pen.lineTo((250, 720))
    pen.lineTo((400, 720))
    pen.lineTo((600, 720))
    pen.closePath()

def draw_search(pen):
    """🔍 Search (same as zoom but no +)"""
    _draw_circle(pen, 420, 580, 320, 250)
    pen.moveTo((620, 340))
    pen.lineTo((900, 60))
    pen.lineTo((940, 100))
    pen.lineTo((660, 380))
    pen.closePath()

def draw_filter(pen):
    """Funnel filter"""
    pen.moveTo((100, 800))
    pen.lineTo((900, 800))
    pen.lineTo((580, 450))
    pen.lineTo((580, 150))
    pen.lineTo((420, 150))
    pen.lineTo((420, 450))
    pen.closePath()

def draw_check(pen):
    """✓ Checkmark"""
    pen.moveTo((150, 500))
    pen.lineTo((400, 200))
    pen.lineTo((470, 260))
    pen.lineTo((280, 500))
    pen.lineTo((850, 800))
    pen.lineTo((780, 860))
    pen.closePath()

def draw_warning(pen):
    """⚠ Warning triangle"""
    pen.moveTo((500, 900))
    pen.lineTo((900, 150))
    pen.lineTo((100, 150))
    pen.closePath()
    # Inner cutout
    pen.moveTo((500, 800))
    pen.lineTo((200, 220))
    pen.lineTo((800, 220))
    pen.closePath()
    # Exclamation
    pen.moveTo((465, 700))
    pen.lineTo((535, 700))
    pen.lineTo((535, 400))
    pen.lineTo((465, 400))
    pen.closePath()
    pen.moveTo((465, 330))
    pen.lineTo((535, 330))
    pen.lineTo((535, 260))
    pen.lineTo((465, 260))
    pen.closePath()

def draw_error(pen):
    """✕ X in circle"""
    _draw_circle(pen, 500, 500, 450, 380)
    t = 40
    pen.moveTo((300, 300 + t))
    pen.lineTo((500 - t, 500))
    pen.lineTo((300, 700 - t))
    pen.lineTo((300 + t, 700))
    pen.lineTo((500, 500 + t))
    pen.lineTo((700 - t, 700))
    pen.lineTo((700, 700 - t))
    pen.lineTo((500 + t, 500))
    pen.lineTo((700, 300 + t))
    pen.lineTo((700 - t, 300))
    pen.lineTo((500, 500 - t))
    pen.lineTo((300 + t, 300))
    pen.closePath()

def draw_info(pen):
    """ℹ Info circle"""
    _draw_circle(pen, 500, 500, 450, 380)
    # i dot
    pen.moveTo((460, 720))
    pen.lineTo((540, 720))
    pen.lineTo((540, 660))
    pen.lineTo((460, 660))
    pen.closePath()
    # i body
    pen.moveTo((460, 600))
    pen.lineTo((540, 600))
    pen.lineTo((540, 300))
    pen.lineTo((460, 300))
    pen.closePath()

def draw_chevron_right(pen):
    """▶ Chevron right"""
    pen.moveTo((300, 850))
    pen.lineTo((700, 500))
    pen.lineTo((300, 150))
    pen.lineTo((380, 100))
    pen.lineTo((780, 500))
    pen.lineTo((380, 900))
    pen.closePath()

def draw_chevron_down(pen):
    """▼ Chevron down"""
    pen.moveTo((150, 650))
    pen.lineTo((500, 250))
    pen.lineTo((850, 650))
    pen.lineTo((900, 600))
    pen.lineTo((500, 150))
    pen.lineTo((100, 600))
    pen.closePath()

def draw_close(pen):
    """✕ Close X"""
    t = 40
    pen.moveTo((150, 150 + t))
    pen.lineTo((500 - t, 500))
    pen.lineTo((150, 850 - t))
    pen.lineTo((150 + t, 850))
    pen.lineTo((500, 500 + t))
    pen.lineTo((850 - t, 850))
    pen.lineTo((850, 850 - t))
    pen.lineTo((500 + t, 500))
    pen.lineTo((850, 150 + t))
    pen.lineTo((850 - t, 150))
    pen.lineTo((500, 500 - t))
    pen.lineTo((150 + t, 150))
    pen.closePath()

def draw_menu(pen):
    """☰ Hamburger menu"""
    for y in [250, 470, 690]:
        pen.moveTo((150, y))
        pen.lineTo((850, y))
        pen.lineTo((850, y + 70))
        pen.lineTo((150, y + 70))
        pen.closePath()

def draw_maximize(pen):
    """□ Maximize"""
    pen.moveTo((150, 150))
    pen.lineTo((150, 850))
    pen.lineTo((850, 850))
    pen.lineTo((850, 150))
    pen.closePath()
    pen.moveTo((220, 780))
    pen.lineTo((780, 780))
    pen.lineTo((780, 220))
    pen.lineTo((220, 220))
    pen.closePath()

def draw_minimize(pen):
    """— Minimize"""
    pen.moveTo((150, 460))
    pen.lineTo((850, 460))
    pen.lineTo((850, 540))
    pen.lineTo((150, 540))
    pen.closePath()

def draw_eye(pen):
    """👁 Eye"""
    # Eye shape (almond using lines)
    pen.moveTo((50, 500))
    pen.lineTo((200, 680))
    pen.lineTo((350, 790))
    pen.lineTo((500, 830))
    pen.lineTo((650, 790))
    pen.lineTo((800, 680))
    pen.lineTo((950, 500))
    pen.lineTo((800, 320))
    pen.lineTo((650, 210))
    pen.lineTo((500, 170))
    pen.lineTo((350, 210))
    pen.lineTo((200, 320))
    pen.closePath()
    # Pupil (cutout - clockwise)
    import math
    steps = 16
    pts = []
    for i in range(steps):
        angle = -math.pi * 2 * i / steps
        pts.append((int(500 + 150 * math.cos(angle)), int(500 + 150 * math.sin(angle))))
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.closePath()

def draw_eye_off(pen):
    """Eye with slash"""
    draw_eye(pen)
    # Slash
    pen.moveTo((200, 800))
    pen.lineTo((800, 200))
    pen.lineTo((840, 240))
    pen.lineTo((240, 840))
    pen.closePath()

def draw_palette(pen):
    """🎨 Palette"""
    _draw_circle(pen, 500, 500, 420, 350)
    # Color dots
    for cx, cy in [(350, 650), (550, 700), (350, 350), (600, 350)]:
        _draw_filled_circle(pen, cx, cy, 50)

def draw_line_width(pen):
    """═ Line width"""
    pen.moveTo((100, 250))
    pen.lineTo((900, 250))
    pen.lineTo((900, 290))
    pen.lineTo((100, 290))
    pen.closePath()
    pen.moveTo((100, 460))
    pen.lineTo((900, 460))
    pen.lineTo((900, 520))
    pen.lineTo((100, 520))
    pen.closePath()
    pen.moveTo((100, 680))
    pen.lineTo((900, 680))
    pen.lineTo((900, 770))
    pen.lineTo((100, 770))
    pen.closePath()

def draw_plus(pen):
    """+ Plus"""
    t = 60
    pen.moveTo((500 - t, 150))
    pen.lineTo((500 + t, 150))
    pen.lineTo((500 + t, 500 - t))
    pen.lineTo((850, 500 - t))
    pen.lineTo((850, 500 + t))
    pen.lineTo((500 + t, 500 + t))
    pen.lineTo((500 + t, 850))
    pen.lineTo((500 - t, 850))
    pen.lineTo((500 - t, 500 + t))
    pen.lineTo((150, 500 + t))
    pen.lineTo((150, 500 - t))
    pen.lineTo((500 - t, 500 - t))
    pen.closePath()

def draw_minus(pen):
    """- Minus"""
    pen.moveTo((150, 460))
    pen.lineTo((850, 460))
    pen.lineTo((850, 540))
    pen.lineTo((150, 540))
    pen.closePath()

def draw_play(pen):
    """▶ Play"""
    pen.moveTo((250, 150))
    pen.lineTo((250, 850))
    pen.lineTo((800, 500))
    pen.closePath()

def draw_pause(pen):
    """⏸ Pause"""
    pen.moveTo((200, 150))
    pen.lineTo((200, 850))
    pen.lineTo((380, 850))
    pen.lineTo((380, 150))
    pen.closePath()
    pen.moveTo((620, 150))
    pen.lineTo((620, 850))
    pen.lineTo((800, 850))
    pen.lineTo((800, 150))
    pen.closePath()

def draw_stop(pen):
    """⏹ Stop"""
    pen.moveTo((200, 200))
    pen.lineTo((200, 800))
    pen.lineTo((800, 800))
    pen.lineTo((800, 200))
    pen.closePath()

def draw_step_forward(pen):
    """⏭ Step forward"""
    pen.moveTo((150, 150))
    pen.lineTo((150, 850))
    pen.lineTo((600, 500))
    pen.closePath()
    pen.moveTo((650, 150))
    pen.lineTo((650, 850))
    pen.lineTo((750, 850))
    pen.lineTo((750, 150))
    pen.closePath()

def draw_step_backward(pen):
    """⏮ Step backward"""
    pen.moveTo((850, 150))
    pen.lineTo((850, 850))
    pen.lineTo((400, 500))
    pen.closePath()
    pen.moveTo((250, 150))
    pen.lineTo((250, 850))
    pen.lineTo((350, 850))
    pen.lineTo((350, 150))
    pen.closePath()

def draw_sun(pen):
    """☀ Sun"""
    import math
    _draw_filled_circle(pen, 500, 500, 200)
    # Rays
    for i in range(8):
        angle = math.pi * 2 * i / 8
        x1 = 500 + int(280 * math.cos(angle))
        y1 = 500 + int(280 * math.sin(angle))
        x2 = 500 + int(420 * math.cos(angle))
        y2 = 500 + int(420 * math.sin(angle))
        dx = int(25 * math.cos(angle + math.pi/2))
        dy = int(25 * math.sin(angle + math.pi/2))
        pen.moveTo((x1 - dx, y1 - dy))
        pen.lineTo((x2 - dx, y2 - dy))
        pen.lineTo((x2 + dx, y2 + dy))
        pen.lineTo((x1 + dx, y1 + dy))
        pen.closePath()

def draw_moon(pen):
    """☽ Moon crescent"""
    import math
    # Outer circle
    steps = 24
    pts = []
    for i in range(steps):
        angle = math.pi * 2 * i / steps
        pts.append((int(500 + 380 * math.cos(angle)), int(500 + 380 * math.sin(angle))))
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.closePath()
    # Inner cutout circle (offset)
    pts2 = []
    for i in range(steps):
        angle = -math.pi * 2 * i / steps
        pts2.append((int(620 + 320 * math.cos(angle)), int(560 + 320 * math.sin(angle))))
    pen.moveTo(pts2[0])
    for pt in pts2[1:]:
        pen.lineTo(pt)
    pen.closePath()

def draw_contrast(pen):
    """◐ Half circle contrast"""
    import math
    steps = 24
    # Full circle
    pts = []
    for i in range(steps):
        angle = math.pi * 2 * i / steps
        pts.append((int(500 + 400 * math.cos(angle)), int(500 + 400 * math.sin(angle))))
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.closePath()
    # Right half cutout
    pts2 = []
    for i in range(steps // 2 + 1):
        angle = -math.pi / 2 - math.pi * i / (steps // 2)
        pts2.append((int(500 + 330 * math.cos(angle)), int(500 + 330 * math.sin(angle))))
    pen.moveTo(pts2[0])
    for pt in pts2[1:]:
        pen.lineTo(pt)
    pen.closePath()

def draw_simple_rect(pen):
    """Generic rectangle placeholder"""
    pen.moveTo((200, 200))
    pen.lineTo((200, 800))
    pen.lineTo((800, 800))
    pen.lineTo((800, 200))
    pen.closePath()
    pen.moveTo((270, 270))
    pen.lineTo((730, 270))
    pen.lineTo((730, 730))
    pen.lineTo((270, 730))
    pen.closePath()

def draw_arrow_up(pen):
    """⬆ Arrow up"""
    pen.moveTo((500, 900))
    pen.lineTo((800, 500))
    pen.lineTo((600, 500))
    pen.lineTo((600, 100))
    pen.lineTo((400, 100))
    pen.lineTo((400, 500))
    pen.lineTo((200, 500))
    pen.closePath()

def draw_arrow_down(pen):
    """⬇ Arrow down"""
    pen.moveTo((500, 100))
    pen.lineTo((200, 500))
    pen.lineTo((400, 500))
    pen.lineTo((400, 900))
    pen.lineTo((600, 900))
    pen.lineTo((600, 500))
    pen.lineTo((800, 500))
    pen.closePath()

def draw_arrow_left(pen):
    """⬅ Arrow left"""
    pen.moveTo((100, 500))
    pen.lineTo((500, 800))
    pen.lineTo((500, 600))
    pen.lineTo((900, 600))
    pen.lineTo((900, 400))
    pen.lineTo((500, 400))
    pen.lineTo((500, 200))
    pen.closePath()

def draw_arrow_right(pen):
    """➡ Arrow right"""
    pen.moveTo((900, 500))
    pen.lineTo((500, 800))
    pen.lineTo((500, 600))
    pen.lineTo((100, 600))
    pen.lineTo((100, 400))
    pen.lineTo((500, 400))
    pen.lineTo((500, 200))
    pen.closePath()

def draw_refresh(pen):
    """🔄 Refresh circular arrow"""
    import math
    cx, cy, r = 500, 500, 350
    steps = 18
    # Arc (3/4 circle)
    outer_pts = []
    inner_pts = []
    for i in range(steps):
        angle = math.pi / 2 + math.pi * 2 * i * 0.75 / (steps - 1)
        outer_pts.append((int(cx + (r + 40) * math.cos(angle)), int(cy + (r + 40) * math.sin(angle))))
        inner_pts.append((int(cx + (r - 40) * math.cos(angle)), int(cy + (r - 40) * math.sin(angle))))
    pen.moveTo(outer_pts[0])
    for pt in outer_pts[1:]:
        pen.lineTo(pt)
    for pt in reversed(inner_pts):
        pen.lineTo(pt)
    pen.closePath()
    # Arrowhead
    pen.moveTo(outer_pts[-1])
    pen.lineTo((outer_pts[-1][0] + 100, outer_pts[-1][1] + 80))
    pen.lineTo((outer_pts[-1][0] - 20, outer_pts[-1][1] + 120))
    pen.closePath()

def draw_home(pen):
    """🏠 Home"""
    # Roof
    pen.moveTo((500, 850))
    pen.lineTo((900, 500))
    pen.lineTo((820, 500))
    pen.lineTo((500, 780))
    pen.lineTo((180, 500))
    pen.lineTo((100, 500))
    pen.closePath()
    # House body
    pen.moveTo((220, 500))
    pen.lineTo((220, 150))
    pen.lineTo((780, 150))
    pen.lineTo((780, 500))
    pen.lineTo((700, 440))
    pen.lineTo((700, 150))
    pen.lineTo((300, 150))
    pen.lineTo((300, 440))
    pen.closePath()
    # Door
    pen.moveTo((420, 150))
    pen.lineTo((420, 350))
    pen.lineTo((580, 350))
    pen.lineTo((580, 150))
    pen.closePath()

def draw_circle_marker(pen):
    """● Filled circle"""
    _draw_filled_circle(pen, 500, 500, 350)

def draw_square_marker(pen):
    """■ Filled square"""
    pen.moveTo((200, 200))
    pen.lineTo((200, 800))
    pen.lineTo((800, 800))
    pen.lineTo((800, 200))
    pen.closePath()

def draw_triangle_marker(pen):
    """▲ Filled triangle"""
    pen.moveTo((500, 850))
    pen.lineTo((850, 150))
    pen.lineTo((150, 150))
    pen.closePath()

def draw_diamond_marker(pen):
    """◆ Filled diamond"""
    pen.moveTo((500, 900))
    pen.lineTo((900, 500))
    pen.lineTo((500, 100))
    pen.lineTo((100, 500))
    pen.closePath()

def draw_edit(pen):
    """✏ Pencil"""
    pen.moveTo((150, 150))
    pen.lineTo((150, 250))
    pen.lineTo((700, 800))
    pen.lineTo((850, 800))
    pen.lineTo((850, 700))
    pen.lineTo((250, 150))
    pen.closePath()
    # Tip
    pen.moveTo((100, 100))
    pen.lineTo((150, 150))
    pen.lineTo((250, 150))
    pen.closePath()

def draw_trash(pen):
    """🗑 Trash can"""
    # Lid
    pen.moveTo((150, 800))
    pen.lineTo((850, 800))
    pen.lineTo((850, 740))
    pen.lineTo((150, 740))
    pen.closePath()
    # Handle
    pen.moveTo((380, 800))
    pen.lineTo((380, 870))
    pen.lineTo((620, 870))
    pen.lineTo((620, 800))
    pen.lineTo((560, 800))
    pen.lineTo((560, 810))
    pen.lineTo((440, 810))
    pen.lineTo((440, 800))
    pen.closePath()
    # Body
    pen.moveTo((200, 700))
    pen.lineTo((250, 150))
    pen.lineTo((750, 150))
    pen.lineTo((800, 700))
    pen.closePath()
    # Lines in body (cutouts)
    for x in [380, 500, 620]:
        pen.moveTo((x - 20, 650))
        pen.lineTo((x + 20, 650))
        pen.lineTo((x + 20, 220))
        pen.lineTo((x - 20, 220))
        pen.closePath()

def draw_grid_icon(pen):
    """⊞ Grid"""
    t = 60
    # Outer
    pen.moveTo((100, 100))
    pen.lineTo((100, 900))
    pen.lineTo((900, 900))
    pen.lineTo((900, 100))
    pen.closePath()
    # Cutout top-left
    pen.moveTo((160, 540))
    pen.lineTo((460, 540))
    pen.lineTo((460, 840))
    pen.lineTo((160, 840))
    pen.closePath()
    # Cutout top-right
    pen.moveTo((540, 540))
    pen.lineTo((840, 540))
    pen.lineTo((840, 840))
    pen.lineTo((540, 840))
    pen.closePath()
    # Cutout bottom-left
    pen.moveTo((160, 160))
    pen.lineTo((460, 160))
    pen.lineTo((460, 460))
    pen.lineTo((160, 460))
    pen.closePath()
    # Cutout bottom-right
    pen.moveTo((540, 160))
    pen.lineTo((840, 160))
    pen.lineTo((840, 460))
    pen.lineTo((540, 460))
    pen.closePath()

def draw_list_icon(pen):
    """☰ List"""
    draw_menu(pen)

def draw_fullscreen(pen):
    """⛶ Fullscreen arrows"""
    t = 60
    # Top-left corner
    pen.moveTo((100, 900))
    pen.lineTo((100, 650))
    pen.lineTo((160, 650))
    pen.lineTo((160, 840))
    pen.lineTo((350, 840))
    pen.lineTo((350, 900))
    pen.closePath()
    # Top-right
    pen.moveTo((650, 900))
    pen.lineTo((900, 900))
    pen.lineTo((900, 650))
    pen.lineTo((840, 650))
    pen.lineTo((840, 840))
    pen.lineTo((650, 840))
    pen.closePath()
    # Bottom-left
    pen.moveTo((100, 350))
    pen.lineTo((100, 100))
    pen.lineTo((350, 100))
    pen.lineTo((350, 160))
    pen.lineTo((160, 160))
    pen.lineTo((160, 350))
    pen.closePath()
    # Bottom-right
    pen.moveTo((840, 350))
    pen.lineTo((840, 160))
    pen.lineTo((650, 160))
    pen.lineTo((650, 100))
    pen.lineTo((900, 100))
    pen.lineTo((900, 350))
    pen.closePath()

def draw_lock(pen):
    """🔒 Lock"""
    # Body
    pen.moveTo((200, 150))
    pen.lineTo((200, 550))
    pen.lineTo((800, 550))
    pen.lineTo((800, 150))
    pen.closePath()
    # Shackle (arch with lines)
    import math
    steps = 12
    outer_pts = []
    inner_pts = []
    for i in range(steps + 1):
        angle = math.pi * i / steps
        outer_pts.append((int(500 + 200 * math.cos(angle)), int(550 + 250 * math.sin(angle))))
        inner_pts.append((int(500 + 130 * math.cos(angle)), int(550 + 200 * math.sin(angle))))
    pen.moveTo(outer_pts[0])
    for pt in outer_pts[1:]:
        pen.lineTo(pt)
    for pt in reversed(inner_pts):
        pen.lineTo(pt)
    pen.closePath()

def draw_file_icon(pen):
    """📄 File"""
    pen.moveTo((200, 100))
    pen.lineTo((200, 900))
    pen.lineTo((800, 900))
    pen.lineTo((800, 350))
    pen.lineTo((550, 100))
    pen.closePath()
    pen.moveTo((270, 830))
    pen.lineTo((730, 830))
    pen.lineTo((730, 380))
    pen.lineTo((520, 170))
    pen.lineTo((270, 170))
    pen.closePath()
    # Fold
    pen.moveTo((550, 100))
    pen.lineTo((550, 350))
    pen.lineTo((800, 350))
    pen.closePath()


# ─── Helper functions ────────────────────────────────────────────────────────

def _draw_circle(pen, cx, cy, r_outer, r_inner, clockwise=False):
    """Draw a ring (circle with hole)."""
    import math
    steps = 24
    # Outer circle (counter-clockwise)
    pts = []
    for i in range(steps):
        angle = math.pi * 2 * i / steps
        pts.append((int(cx + r_outer * math.cos(angle)), int(cy + r_outer * math.sin(angle))))
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.closePath()
    if r_inner > 0:
        # Inner circle (clockwise for cutout)
        pts2 = []
        for i in range(steps):
            angle = -math.pi * 2 * i / steps
            pts2.append((int(cx + r_inner * math.cos(angle)), int(cy + r_inner * math.sin(angle))))
        pen.moveTo(pts2[0])
        for pt in pts2[1:]:
            pen.lineTo(pt)
        pen.closePath()

def _draw_filled_circle(pen, cx, cy, r):
    """Draw a filled circle."""
    import math
    steps = 24
    pts = []
    for i in range(steps):
        angle = math.pi * 2 * i / steps
        pts.append((int(cx + r * math.cos(angle)), int(cy + r * math.sin(angle))))
    pen.moveTo(pts[0])
    for pt in pts[1:]:
        pen.lineTo(pt)
    pen.closePath()


# ─── Icon mapping ────────────────────────────────────────────────────────────

ICON_MAP = {
    0xE001: ("ChartLine", draw_chart_line),
    0xE002: ("ScatterChart", draw_scatter_chart),
    0xE003: ("Axes", draw_axes),
    0xE004: ("Wrench", draw_wrench),
    0xE005: ("Folder", draw_folder),
    0xE006: ("Settings", draw_settings),
    0xE007: ("Help", draw_help),
    0xE008: ("ZoomIn", draw_zoom_in),
    0xE009: ("Hand", draw_hand),
    0xE00A: ("Ruler", draw_ruler),
    0xE00B: ("Crosshair", draw_crosshair),
    0xE00C: ("Pin", draw_pin),
    0xE00D: ("Type", draw_type),
    0xE00E: ("Export", draw_export),
    0xE00F: ("Save", draw_save),
    0xE010: ("Copy", draw_copy),
    0xE011: ("Undo", draw_undo),
    0xE012: ("Redo", draw_redo),
    0xE013: ("Search", draw_search),
    0xE014: ("Filter", draw_filter),
    0xE015: ("Check", draw_check),
    0xE016: ("Warning", draw_warning),
    0xE017: ("Error", draw_error),
    0xE018: ("Info", draw_info),
    0xE019: ("ChevronRight", draw_chevron_right),
    0xE01A: ("ChevronDown", draw_chevron_down),
    0xE01B: ("Close", draw_close),
    0xE01C: ("Menu", draw_menu),
    0xE01D: ("Maximize", draw_maximize),
    0xE01E: ("Minimize", draw_minimize),
    0xE01F: ("Eye", draw_eye),
    0xE020: ("EyeOff", draw_eye_off),
    0xE021: ("Palette", draw_palette),
    0xE022: ("LineWidth", draw_line_width),
    0xE023: ("Plus", draw_plus),
    0xE024: ("Minus", draw_minus),
    0xE025: ("Play", draw_play),
    0xE026: ("Pause", draw_pause),
    0xE027: ("Stop", draw_stop),
    0xE028: ("StepForward", draw_step_forward),
    0xE029: ("StepBackward", draw_step_backward),
    0xE02A: ("Sun", draw_sun),
    0xE02B: ("Moon", draw_moon),
    0xE02C: ("Contrast", draw_contrast),
    0xE02D: ("Layout", draw_simple_rect),
    0xE02E: ("SplitHorizontal", draw_axes),
    0xE02F: ("SplitVertical", draw_axes),
    0xE030: ("Tab", draw_simple_rect),
    0xE031: ("LineChart", draw_chart_line),
    0xE032: ("BarChart", draw_scatter_chart),
    0xE033: ("PieChart", draw_circle_marker),
    0xE034: ("Heatmap", draw_grid_icon),
    0xE035: ("ArrowUp", draw_arrow_up),
    0xE036: ("ArrowDown", draw_arrow_down),
    0xE037: ("ArrowLeft", draw_arrow_left),
    0xE038: ("ArrowRight", draw_arrow_right),
    0xE039: ("Refresh", draw_refresh),
    0xE03A: ("Clock", draw_circle_marker),  # placeholder
    0xE03B: ("Calendar", draw_grid_icon),   # placeholder
    0xE03C: ("Tag", draw_triangle_marker),
    0xE03D: ("Link", draw_simple_rect),
    0xE03E: ("Unlink", draw_simple_rect),
    0xE03F: ("Lock", draw_lock),
    0xE040: ("Unlock", draw_lock),  # same shape for now
    0xE041: ("Command", draw_simple_rect),
    0xE042: ("Keyboard", draw_simple_rect),
    0xE043: ("Shortcut", draw_arrow_up),
    0xE044: ("FolderOpen", draw_folder),
    0xE045: ("File", draw_file_icon),
    0xE046: ("FileText", draw_file_icon),
    0xE047: ("Grid", draw_grid_icon),
    0xE048: ("List", draw_list_icon),
    0xE049: ("Fullscreen", draw_fullscreen),
    0xE04A: ("FullscreenExit", draw_fullscreen),
    0xE04B: ("Edit", draw_edit),
    0xE04C: ("Trash", draw_trash),
    0xE04D: ("Duplicate", draw_copy),
    0xE04E: ("Function", draw_simple_rect),
    0xE04F: ("Integral", draw_simple_rect),
    0xE050: ("Sigma", draw_simple_rect),
    0xE051: ("Sqrt", draw_simple_rect),
    0xE052: ("Circle", draw_circle_marker),
    0xE053: ("Square", draw_square_marker),
    0xE054: ("Triangle", draw_triangle_marker),
    0xE055: ("Diamond", draw_diamond_marker),
    0xE056: ("Cross", draw_close),
    0xE057: ("PlusMarker", draw_plus),
    0xE058: ("MinusMarker", draw_minus),
    0xE059: ("Asterisk", draw_close),  # reuse X shape
    0xE05A: ("LineSolid", draw_minus),
    0xE05B: ("LineDashed", draw_minus),
    0xE05C: ("LineDotted", draw_minus),
    0xE05D: ("LineDashDot", draw_minus),
    0xE05E: ("Home", draw_home),
    0xE05F: ("Back", draw_arrow_left),
    0xE060: ("Forward", draw_arrow_right),
    0xE061: ("Up", draw_arrow_up),
    0xE062: ("Down", draw_arrow_down),
}


def build_font():
    """Build a TTF font with all icon glyphs."""
    fb = FontBuilder(UNITS_PER_EM, isTTF=True)
    
    glyph_names = [".notdef"] + [f"icon_{cp:04X}" for cp in sorted(ICON_MAP.keys())]
    fb.setupGlyphOrder(glyph_names)
    
    cmap = {}
    for cp in sorted(ICON_MAP.keys()):
        cmap[cp] = f"icon_{cp:04X}"
    fb.setupCharacterMap(cmap)
    
    # Draw glyphs using TTGlyphPen
    glyph_dict = {}
    
    # .notdef = empty glyph
    notdef_pen = TTGlyphPen(None)
    glyph_dict[".notdef"] = notdef_pen.glyph()
    
    for cp in sorted(ICON_MAP.keys()):
        name, draw_fn = ICON_MAP[cp]
        glyph_name = f"icon_{cp:04X}"
        pen = TTGlyphPen(None)
        try:
            draw_fn(pen)
        except Exception as e:
            print(f"Warning: Failed to draw {name} (0x{cp:04X}): {e}")
            pen = TTGlyphPen(None)
            draw_simple_rect(pen)
        glyph_dict[glyph_name] = pen.glyph()
    
    fb.setupGlyf(glyph_dict)
    
    metrics = {}
    for gname in glyph_names:
        metrics[gname] = (UNITS_PER_EM, 0)
    fb.setupHorizontalMetrics(metrics)
    
    fb.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    fb.setupNameTable({
        "familyName": "SpectraIcons",
        "styleName": "Regular",
    })
    fb.setupOS2(
        sTypoAscender=ASCENT,
        sTypoDescender=DESCENT,
        usWinAscent=ASCENT,
        usWinDescent=-DESCENT,
    )
    fb.setupPost()
    
    return fb.font


def generate_cpp_header(font_data: bytes, output_path: Path):
    """Generate C++ header with raw font data for ImGui's AddFontFromMemoryTTF."""
    lines = []
    lines.append("#pragma once")
    lines.append("// Auto-generated icon font data for Spectra")
    lines.append(f"// Font size: {len(font_data)} bytes")
    lines.append("// Codepoint range: U+E001 - U+E062 (Private Use Area)")
    lines.append("")
    lines.append(f"static const unsigned int SpectraIcons_size = {len(font_data)};")
    lines.append(f"static const unsigned char SpectraIcons_data[{len(font_data)}] = {{")
    
    for i in range(0, len(font_data), 16):
        chunk = font_data[i:i+16]
        hex_vals = ",".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    
    lines.append("};")
    lines.append("")
    
    with open(output_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    
    print(f"C++ header written to: {output_path}")
    print(f"  Size: {len(font_data)} bytes")


def main():
    print("Building Spectra icon font...")
    font = build_font()
    
    # Save TTF for debugging
    out_dir = Path(__file__).parent.parent / "third_party"
    ttf_path = out_dir / "SpectraIcons.ttf"
    font.save(str(ttf_path))
    print(f"Font saved to: {ttf_path}")
    
    # Read font bytes
    with open(ttf_path, "rb") as f:
        font_data = f.read()
    
    # Generate C++ header
    hpp_path = out_dir / "icon_font_data.hpp"
    generate_cpp_header(font_data, hpp_path)
    
    print(f"\nIcon count: {len(ICON_MAP)}")
    print(f"Codepoint range: U+E001 - U+E062")
    print("Done!")


if __name__ == "__main__":
    main()
