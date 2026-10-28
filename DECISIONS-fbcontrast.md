# Decisions — fbcontrast (S4-C01: rendered framebuffer contrast)

- **Metric.** Contrast is measured on rendered pixels as the peak-ink pixel of a known region against that
  region's dominant (plots) or ring-median (UI text) colour, using `ui::Color::contrast_ratio` (WCAG 2.1).
  Thin elements (spines, the 1px series line) use the median over 1px strips of the per-strip peak, so a
  crossing grid line or series cannot pass or fail them on its own. Text also needs >= 12·s² near-peak pixels.
- **Plot targets.** Text (title, x/y tick labels) >= 4.5:1 vs the figure background; spines, 1px line and
  markers >= 3:1 vs the canvas; 4 themes × 1x and 2x export (`save_png` at 2× size is the API's scale path).
  The grid is deliberately not asserted (it is meant to stay below 3:1; High Contrast grid alphas are being
  lowered in another package).
- **CVD threshold.** Rendered line colours of `colorblind`, `tol_bright`, `tol_muted` on every theme keep a
  pairwise OKLab ΔE >= 0.04 under normal vision and simulated protanopia/deuteranopia (Machado et al. 2009,
  severity 1.0, linear RGB; the matrices match `ui::simulate_cvd`, whose comment calls them Viénot). 0.04 is
  the floor `test_palette_visibility` already holds source palettes to. Tightest measured: colorblind on Light
  protanopia 0.041, tol_muted on Light deuteranopia 0.045.
- **Fix: Night axis spine.** Rendered Night spine was 2.26:1 (bottom) / 2.00:1 (left, 1x) on the canvas.
  `night.axis_line` alpha 0.38 -> 0.70 (hue unchanged): 4.11 / 3.89. This intentionally changes every
  Night-rendered golden: 51 baselines regenerated (the two whose old baseline still passes were left alone).
- **UI text sampling.** Glyph quads are read from each ImGui window's draw list after the captured frame
  (font-atlas quads, clipped to the draw command's clip rect, grouped into runs). Only runs of 3+ glyphs whose
  2px surrounding ring is >= 60% one colour, and that are not covered by a popup / tooltip / modal / floating
  titled window drawn above (or dimmed by a modal), are asserted. Every capture's runs are logged to
  `design/text_contrast.csv` for diagnosis.
- **UI states asserted (4.5:1):** 07 inspector (glass), 08 command palette (glass over blur), 11 nav rail +
  command bar (Dark), 12 command bar (Light), 15 hover tooltip, 18 timeline, 36 File menu popup,
  54 palette incl. the row under the selection highlight, 57–62 Settings modals (3 themes).
- **Fixes from UI measurements (minimal, alpha only):** text_tertiary is already the de-emphasised tier and
  is contracted >= 4.5:1 opaque, so its extra fades were removed: Inspector / Timeline eyebrows and "TRACKS"
  (0.75 -> 1.0, measured 4.19 / 4.43), palette category headers (0.8 -> 1.0, 3.53); tooltip key labels
  (text_secondary 0.70/0.75 -> 1.0, Light 4.01); nav rail label alpha tokens (0.78/0.95 -> 1.0, Light rail foot
  2.9). Status-bar pills push their (semantic-colour) text to 4.5:1 after the bar's text alpha with
  `ensure_contrast` (Light "Pan" chip 3.22 -> passes).
- **Theme transition.** New state 63 captures the exact midpoint of Dark -> Light (long transition advanced
  with `update()`, timing independent). Plain lerp measured ~1.0–1.2:1 for all text. `interpolate_colors` now
  applies `ensure_contrast(…, 4.5)` to text_primary/secondary/tertiary on bg_primary and bg_secondary and to
  tick_label on bg_canvas; measured 3.5–5.6:1 at the midpoint. Asserted floor for this transient frame is
  3:1 on neutral (text-token) ink: against a mid-grey surface 4.5:1 is not reachable for every surface at once.
  Unit test `TransitionKeepsTextReadableOnEveryFrame` checks all 12 theme pairs × 19 steps at the source level.
- **Disabled controls.** New state 64 opens the custom transform dialog (new `ImGuiIntegration::
  open_transform_dialog()`, same as the rail button) with an empty formula, scrolled to its action row, so
  "Save Preset"/"Apply" are drawn with `BeginDisabled`. Disabled = text alpha < 0xB0. Disabled controls are
  exempt from WCAG 1.4.3; asserted floor 3:1 (measured 5.02 / 3.16). `ImGuiCol_TextDisabled` (=text_tertiary)
  is used for live labels here, so it is held to 4.5:1, not treated as disabled.
- **Design-review shot count** 63 -> 65 (`EXPECTED_DESIGN_SHOTS`, comments).
- **Not fixed (reported):** accent colours used as text are not covered by the transition guard (active nav
  label measures 1.19:1 at the midpoint); Light nav rail labels at 11.5px lose up to ~30% contrast to
  antialiasing (neutral min 4.26 rendered for 6.46 specified) so the Light rail is not asserted at 4.5;
  Light active rail label 3.92 rendered (4.90 specified); Light status fps/GPU readouts 4.04 / 4.27 rendered
  (4.49 / 4.67 specified).
