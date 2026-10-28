# DECISIONS — hcgrid (S4-U02: High Contrast grid too prominent in dense scenes)

- High Contrast major grid alpha 0.26 -> 0.20 (white over black reads #424242 -> #333333,
  ~2.10:1 -> ~1.66:1) and minor 0.08 -> 0.065 (#141414 -> #111111, ~1.14:1 -> ~1.11:1). A moderate
  cut: the grid recedes behind data in dense multi-panel layouts but stays clearly visible; minor
  stays faint-but-visible and major keeps ~3x the minor alpha so the two levels stay distinguishable.
- `grid_line` (legacy alias of the major grid) follows the major value (0.20).
- Grid stays translucent white (not opaque grey) to keep the earlier no-checkerboard behaviour over
  translucent fills. Data, axes/spines, ticks and text are untouched (full 21:1).
- The major grid remains intentionally below the WCAG 3:1 non-text level; it is a non-essential
  reference behind labelled axes. The theme comment says so; the contrast tests now bound the major
  grid to [1.5, 1.8]:1 and the minor to (1.05, 1.2]:1 instead of requiring major >= 2:1.
- Other themes (dark, night, light) are not changed; verified by pixel-identical dark renders before/after.
- 3D tick marks are derived from `grid_major` in `render_3d.cpp` for every theme, so in High Contrast
  they get slightly dimmer too. Left as is (out of scope for this package; it is pre-existing
  coupling in all themes), noted here for follow-up.
- No golden baseline uses the High Contrast theme, so no baselines were regenerated.
