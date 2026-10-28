# Decisions — legend (S4-U01)

- An inside legend (every position except OutsideRight) may cover at most 35% of its plot area
  (`legend_layout::INSIDE_MAX_COVERAGE`). Rows are dropped first and the width (column count) stays
  the same; the entries that no longer fit fold into the existing "+N more" cell. The explicit corner
  is kept. Font size is unchanged, so labels are not shrunk to fit.
- A legend that is already summarised ("+N more"), or that is over the area cap, is also kept
  within 50% of the plot height (`INSIDE_MAX_HEIGHT`). Without this, a single tall column (the
  window legend in the gallery scene) passes the area cap but still hides one side of every curve.
  When there is spare width, this height cap may add columns. Complete legends that need no "+N more"
  are left alone, so ordinary legends and goldens do not change.
- The cap never cuts a legend below 4 rows (`INSIDE_MIN_ROWS`), or below the rows it already had.
  Short legends in small subplots therefore stay complete, even when they cover more than 35% of
  the cell.
- The cap is in one shared helper (`legend_layout::compute_legend_grid`). The window (ImGui), GPU/PNG
  and SVG legends all call it, so all three truncate the same way.
- `inside_coverage()`, which decides when a Best legend moves outside, still measures the
  *uncapped* legend. If it measured the capped one, the coverage could never exceed the 0.4
  auto-outside threshold.
- The cap is not configurable through the public `LegendConfig`, so the public API stays the same.
  A user who needs every entry visible can use `LegendPosition::OutsideRight` or the "+N more"
  popup.
- Window legend: hovering a truncated legend and turning the mouse wheel scrolls the visible run
  of entries by 3 entries per notch. The scroll is clamped so the box is always full. Fractional
  trackpad deltas build up and round to the nearest entry. The "+N more" cell keeps its count, and
  its tooltip lists the hidden entries before and after the visible run. A thin thumb in the box's
  right padding shows the scroll position. Clicking "+N more" still opens the full legend popup.
  Static exports (PNG/SVG) always show the first entries (offset 0).
- Window, PNG and SVG use the same truncation policy. Each path still measures labels with its own
  text metrics (ImGui font + eye icon, the GPU MSDF renderer, Inter metrics for SVG), so in the
  gallery scene the shown counts are 15 / 17 / 11. Before this change they were 30 / 47 / 23.
  Unifying the per-path text metrics is outside this package.
