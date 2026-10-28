# Decisions — harness (S4-Q01, S4-Q02)

- **Zoom target via real hover.** Scenes 16 and 35 make the measured axes the input target by moving the real
  pointer over its plot area through the GLFW cursor callback chain (the same hit-test path a user takes before
  a keyboard zoom), not by calling `InputHandler::set_active_axes()` directly. The pointer is placed at 37%/71% of
  the plot area, not at the centre. The pointer is restored after `view.home`.
- **Zoom assertion is exact, not a bound.** The old `< 0.8 × span` check is replaced by `span ratio == 0.75^5 ± 1e-3`
  on the same axes object, plus "command target is the measured axes" and "measured axes is the active figure's plot".
  Scene 35 gets the same treatment because it had the same unestablished-target precondition.
- **Settle budget semantics.** `wait_until_stable` (tests/qa/qa_settle.hpp) gives up only after **both** the frame
  budget (180) and the wall budget (5 s) are spent. That way a slow software rasterizer still gets enough frames and a
  fast GPU still gets enough elapsed time for dt-based animations. A hard cap of 20000 frames covers a stalled clock.
  The predicate must hold for 2 consecutive frames.
- **Timeout reporting.** A timed-out wait is logged to stderr. It is queued as a failed check on the **next named
  capture**, so the named state fails the gate instead of being captured mid-transition.
- **Layout "settled" = `LayoutManager::is_animating() == false`.** This is the product's own definition. It uses the
  window-fitted targets, so an auto-collapsed nav rail at 320 px counts as settled, whereas the user-intent
  `nav_rail_width()` would not. The scene-50 assertion (`nav_rail_animated_width ≈ nav_rail_width ± 0.5`) is unchanged.
- **Waits converted.** Each of these assertions measures something animated or asynchronous, so its fixed wait became
  a bounded predicate wait:
  - inspector slide: 07, 27, 28, 38, 47
  - theme transition: 11, 12, 57–62
  - crosshair fade: `settle_overlays`, used by 15 and 34
  - nav rail / layout: 39, 49, 50
  - swapchain extent after resize: 41–44, 45/45b, 56
  - window positions: 46, 48
  - UI scale in effect: 55

  Fixed waits were left as they were in two cases. Waits before purely boolean/state assertions (menu open, palette
  query, timeline tracks) don't race an animation. Cleanup waits are followed by no assertion. The non-design
  scenarios assert no animated geometry.
- **`run_design_review.sh` repeat knobs.** `SPECTRA_QA_SEED` (default 42) and `SPECTRA_QA_SCREEN` (Xvfb screen,
  default 1920x1200x24) were added so independent repeats can use another seed. They can also start the pointer
  off-window (`3840x2400x24`), which deterministically reproduces the original `16_zoomed_in` failure on the old
  harness. Defaults, and therefore the ctest, are unchanged.
