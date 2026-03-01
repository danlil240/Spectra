/*
 * spectra_embed_c.h — C API for the Spectra Embedding Surface.
 *
 * This provides a pure-C interface to EmbedSurface, suitable for FFI
 * from Python (ctypes), Rust, C#, etc.
 *
 * Usage from C:
 *   SpectraEmbed* s = spectra_embed_create(800, 600);
 *   SpectraFigure* fig = spectra_embed_figure(s);
 *   SpectraAxes* ax = spectra_figure_subplot(fig, 1, 1, 1);
 *   float x[] = {0,1,2,3,4};
 *   float y[] = {0,1,4,9,16};
 *   spectra_axes_line(ax, x, y, 5, NULL);
 *   uint8_t* pixels = malloc(800 * 600 * 4);
 *   spectra_embed_render(s, pixels);
 *   spectra_embed_destroy(s);
 */

#ifndef SPECTRA_EMBED_C_H
#define SPECTRA_EMBED_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct SpectraEmbed  SpectraEmbed;
typedef struct SpectraFigure SpectraFigure;
typedef struct SpectraAxes   SpectraAxes;
typedef struct SpectraSeries SpectraSeries;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

/* Create an embed surface with the given dimensions. Returns NULL on failure. */
SpectraEmbed* spectra_embed_create(uint32_t width, uint32_t height);

/* Destroy an embed surface and free all resources. */
void spectra_embed_destroy(SpectraEmbed* s);

/* Returns 1 if the surface is valid, 0 otherwise. */
int spectra_embed_is_valid(const SpectraEmbed* s);

/* ── Figure management ─────────────────────────────────────────────────── */

/* Create a new figure. Returns NULL on failure. */
SpectraFigure* spectra_embed_figure(SpectraEmbed* s);

/* Get the active figure. Returns NULL if none. */
SpectraFigure* spectra_embed_active_figure(SpectraEmbed* s);

/* ── Axes management ───────────────────────────────────────────────────── */

/* Create a subplot (1-based indexing). Returns NULL on failure. */
SpectraAxes* spectra_figure_subplot(SpectraFigure* fig, int rows, int cols, int index);

/* Create a 3D subplot (1-based indexing). Returns NULL on failure. */
SpectraAxes* spectra_figure_subplot3d(SpectraFigure* fig, int rows, int cols, int index);

/* ── Series creation ───────────────────────────────────────────────────── */

/* Add a line series. label can be NULL. Returns NULL on failure. */
SpectraSeries* spectra_axes_line(SpectraAxes* ax, const float* x, const float* y,
                                  uint32_t count, const char* label);

/* Add a scatter series. label can be NULL. Returns NULL on failure. */
SpectraSeries* spectra_axes_scatter(SpectraAxes* ax, const float* x, const float* y,
                                     uint32_t count, const char* label);

/* ── Series data update ────────────────────────────────────────────────── */

/* Update X data for an existing series. */
void spectra_series_set_x(SpectraSeries* s, const float* x, uint32_t count);

/* Update Y data for an existing series. */
void spectra_series_set_y(SpectraSeries* s, const float* y, uint32_t count);

/* Update both X and Y data atomically (no intermediate mismatch). */
void spectra_series_set_data(SpectraSeries* s, const float* x, const float* y, uint32_t count);

/* ── Rendering ─────────────────────────────────────────────────────────── */

/* Render one frame to RGBA buffer. Buffer must be width*height*4 bytes.
 * Returns 1 on success, 0 on failure. */
int spectra_embed_render(SpectraEmbed* s, uint8_t* out_rgba);

/* Resize the surface. Returns 1 on success, 0 on failure. */
int spectra_embed_resize(SpectraEmbed* s, uint32_t width, uint32_t height);

/* Get current dimensions. */
uint32_t spectra_embed_width(const SpectraEmbed* s);
uint32_t spectra_embed_height(const SpectraEmbed* s);

/* ── Input forwarding ──────────────────────────────────────────────────── */

void spectra_embed_mouse_move(SpectraEmbed* s, float x, float y);
void spectra_embed_mouse_button(SpectraEmbed* s, int button, int action, int mods,
                                 float x, float y);
void spectra_embed_scroll(SpectraEmbed* s, float dx, float dy, float cx, float cy);
void spectra_embed_key(SpectraEmbed* s, int key, int action, int mods);

/* Advance animations by dt seconds. */
void spectra_embed_update(SpectraEmbed* s, float dt);

/* ── Display configuration ────────────────────────────────────────────── */

/* Set DPI scale factor (1.0 = 96 DPI, 2.0 = Retina/HiDPI).
 * Affects text size, tick length, and line widths. */
void spectra_embed_set_dpi_scale(SpectraEmbed* s, float scale);

/* Get current DPI scale factor. */
float spectra_embed_get_dpi_scale(const SpectraEmbed* s);

/* ── Theme & UI chrome ────────────────────────────────────────────────── */

/* Set theme ("dark" or "light"). */
void spectra_embed_set_theme(SpectraEmbed* s, const char* theme);

/* Show/hide UI chrome elements (requires ImGui build). */
void spectra_embed_set_show_command_bar(SpectraEmbed* s, int visible);
void spectra_embed_set_show_status_bar(SpectraEmbed* s, int visible);
void spectra_embed_set_show_nav_rail(SpectraEmbed* s, int visible);
void spectra_embed_set_show_inspector(SpectraEmbed* s, int visible);

/* ── Axes configuration ───────────────────────────────────────────────── */

/* Set axis labels. */
void spectra_axes_set_xlabel(SpectraAxes* ax, const char* label);
void spectra_axes_set_ylabel(SpectraAxes* ax, const char* label);
void spectra_axes_set_title(SpectraAxes* ax, const char* title);

/* Set axis limits (manual zoom). */
void spectra_axes_set_xlim(SpectraAxes* ax, float min, float max);
void spectra_axes_set_ylim(SpectraAxes* ax, float min, float max);

/* Enable/disable grid. */
void spectra_axes_set_grid(SpectraAxes* ax, int enabled);

/* ── Figure configuration ─────────────────────────────────────────────── */

/* Set figure title. */
void spectra_figure_set_title(SpectraFigure* fig, const char* title);

/* ── Easy Render API ───────────────────────────────────────────────────── */
/* One-call offscreen rendering. No surface/figure/axes management needed. */
/* Caller must free returned buffer with spectra_free_pixels().            */

/* Render a line plot to RGBA pixels. Returns pixel buffer (width*height*4).
 * out_width and out_height are set to the rendered dimensions.
 * Returns NULL on failure. Caller must call spectra_free_pixels(). */
uint8_t* spectra_render_line(const float* x, const float* y, uint32_t count,
                              uint32_t width, uint32_t height,
                              uint32_t* out_width, uint32_t* out_height);

/* Render a scatter plot to RGBA pixels. Same semantics as spectra_render_line. */
uint8_t* spectra_render_scatter(const float* x, const float* y, uint32_t count,
                                 uint32_t width, uint32_t height,
                                 uint32_t* out_width, uint32_t* out_height);

/* Render a line plot and save directly to PNG. Returns 1 on success. */
int spectra_render_line_png(const float* x, const float* y, uint32_t count,
                             uint32_t width, uint32_t height,
                             const char* path);

/* Render a scatter plot and save directly to PNG. Returns 1 on success. */
int spectra_render_scatter_png(const float* x, const float* y, uint32_t count,
                                uint32_t width, uint32_t height,
                                const char* path);

/* Free a pixel buffer returned by spectra_render_*() functions. */
void spectra_free_pixels(uint8_t* pixels);

/* ── Constants ─────────────────────────────────────────────────────────── */

#define SPECTRA_MOUSE_LEFT   0
#define SPECTRA_MOUSE_RIGHT  1
#define SPECTRA_MOUSE_MIDDLE 2

#define SPECTRA_ACTION_RELEASE 0
#define SPECTRA_ACTION_PRESS   1

#define SPECTRA_MOD_SHIFT   0x0001
#define SPECTRA_MOD_CONTROL 0x0002
#define SPECTRA_MOD_ALT     0x0004

#define SPECTRA_KEY_ESCAPE 256
#define SPECTRA_KEY_R      82
#define SPECTRA_KEY_G      71
#define SPECTRA_KEY_A      65
#define SPECTRA_KEY_S      83

#ifdef __cplusplus
}
#endif

#endif /* SPECTRA_EMBED_C_H */
