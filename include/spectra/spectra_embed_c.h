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
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Opaque handles */
    typedef struct SpectraEmbed  SpectraEmbed;
    typedef struct SpectraFigure SpectraFigure;
    typedef struct SpectraAxes   SpectraAxes;
    typedef struct SpectraSeries SpectraSeries;

    /* ── Enumerations (mirror the C++ core) ──────────────────────────────── */

    /* LineStyle (spectra::LineStyle) */
    enum
    {
        SPECTRA_LINE_NONE   = 0,
        SPECTRA_LINE_SOLID  = 1,
        SPECTRA_LINE_DASHED = 2,
        SPECTRA_LINE_DOTTED = 3
    };

    /* MarkerStyle (spectra::MarkerStyle) */
    enum
    {
        SPECTRA_MARKER_NONE           = 0,
        SPECTRA_MARKER_CIRCLE         = 1,
        SPECTRA_MARKER_PLUS           = 2,
        SPECTRA_MARKER_CROSS          = 3,
        SPECTRA_MARKER_STAR           = 4,
        SPECTRA_MARKER_SQUARE         = 5,
        SPECTRA_MARKER_DIAMOND        = 6,
        SPECTRA_MARKER_TRIANGLE_UP    = 7,
        SPECTRA_MARKER_TRIANGLE_DOWN  = 8,
        SPECTRA_MARKER_TRIANGLE_LEFT  = 9,
        SPECTRA_MARKER_TRIANGLE_RIGHT = 10
    };

    /* Bar orientation (spectra::BarOrientation) */
    enum
    {
        SPECTRA_BAR_VERTICAL   = 0,
        SPECTRA_BAR_HORIZONTAL = 1
    };

    /* Colormap (spectra::ColormapType) */
    enum
    {
        SPECTRA_CMAP_NONE      = 0,
        SPECTRA_CMAP_VIRIDIS   = 1,
        SPECTRA_CMAP_PLASMA    = 2,
        SPECTRA_CMAP_INFERNO   = 3,
        SPECTRA_CMAP_MAGMA     = 4,
        SPECTRA_CMAP_JET       = 5,
        SPECTRA_CMAP_COOLWARM  = 6,
        SPECTRA_CMAP_GRAYSCALE = 7
    };

    /* Legend position (spectra::LegendPosition) */
    enum
    {
        SPECTRA_LEGEND_TOP_RIGHT    = 0,
        SPECTRA_LEGEND_TOP_LEFT     = 1,
        SPECTRA_LEGEND_BOTTOM_RIGHT = 2,
        SPECTRA_LEGEND_BOTTOM_LEFT  = 3,
        SPECTRA_LEGEND_NONE         = 4
    };

    /* ── Lifecycle ─────────────────────────────────────────────────────────── */

    /* Create an embed surface with the given dimensions. Returns NULL on failure. */
    SpectraEmbed* spectra_embed_create(uint32_t width, uint32_t height);

    /* Create an embed surface with extended configuration.
     * theme        - theme name ("dark", "night", or "light"), or NULL for default.
     * dpi_scale    - DPI scale factor (1.0 = 96 DPI, 2.0 = Retina/HiDPI).
     * msaa         - MSAA samples (1 = no MSAA, 4 = 4x MSAA).
     * bg_alpha     - Background alpha (1.0 = opaque, 0.0 = transparent).
     * Returns NULL on failure. */
    SpectraEmbed* spectra_embed_create_ex(uint32_t    width,
                                          uint32_t    height,
                                          const char* theme,
                                          float       dpi_scale,
                                          uint32_t    msaa,
                                          float       bg_alpha);

    /* Full configuration struct mirroring spectra::EmbedConfig.
     * All boolean fields use int (0 = false, non-zero = true). */
    typedef struct SpectraEmbedConfig
    {
        uint32_t    width;
        uint32_t    height;
        uint32_t    msaa;
        float       dpi_scale;
        float       background_alpha;
        const char* theme;            /* NULL → "night" */
        int         show_imgui_chrome;
        int         show_command_bar;
        int         show_status_bar;
        int         show_nav_rail;
        int         show_inspector;
        int         show_legend;
        int         show_crosshair;
    } SpectraEmbedConfig;

    /* Initialize a SpectraEmbedConfig with library defaults. */
    void spectra_embed_config_default(SpectraEmbedConfig* cfg);

    /* Create an embed surface from a full configuration struct.
     * Returns NULL on failure or if cfg is NULL. */
    SpectraEmbed* spectra_embed_create_config(const SpectraEmbedConfig* cfg);

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
    SpectraSeries* spectra_axes_line(SpectraAxes* ax,
                                     const float* x,
                                     const float* y,
                                     uint32_t     count,
                                     const char*  label);

    /* Add a scatter series. label can be NULL. Returns NULL on failure. */
    SpectraSeries* spectra_axes_scatter(SpectraAxes* ax,
                                        const float* x,
                                        const float* y,
                                        uint32_t     count,
                                        const char*  label);

    /* ── Series data update ────────────────────────────────────────────────── */

    /* Update X data for an existing series. */
    void spectra_series_set_x(SpectraSeries* s, const float* x, uint32_t count);

    /* Update Y data for an existing series. */
    void spectra_series_set_y(SpectraSeries* s, const float* y, uint32_t count);

    /* Update both X and Y data atomically (no intermediate mismatch). */
    void spectra_series_set_data(SpectraSeries* s, const float* x, const float* y, uint32_t count);

    /* ── Series styling (Phase 1A) ─────────────────────────────────────────── */

    /* Set series RGBA color (components in [0,1]). */
    void spectra_series_set_color(SpectraSeries* s, float r, float g, float b, float a);
    /* Set overall series opacity [0,1]. */
    void spectra_series_set_opacity(SpectraSeries* s, float v);
    /* Set line width in pixels (line / 3D-line series). */
    void spectra_series_set_line_width(SpectraSeries* s, float v);
    /* Set marker size in pixels. */
    void spectra_series_set_marker_size(SpectraSeries* s, float v);
    /* Set marker style (SPECTRA_MARKER_*). */
    void spectra_series_set_marker_style(SpectraSeries* s, int style);
    /* Set line style (SPECTRA_LINE_*). */
    void spectra_series_set_line_style(SpectraSeries* s, int style);
    /* Set series legend label. */
    void spectra_series_set_label(SpectraSeries* s, const char* label);

    /* ── Series streaming helpers (Phase 1G) ──────────────────────────────── */

    /* Append a single (x,y) point to a line/scatter series. */
    void spectra_series_append_xy(SpectraSeries* s, float x, float y);
    /* Append a batch of points to a line/scatter series. */
    void spectra_series_append_data(SpectraSeries* s, const float* x, const float* y, uint32_t count);
    /* Configure a ring-buffer capacity (max retained points). 0 = unbounded.
     * When exceeded by appends, the oldest points are dropped. */
    void spectra_series_set_capacity(SpectraSeries* s, uint32_t max_points);
    /* Remove all data points from a line/scatter series. */
    void spectra_series_clear(SpectraSeries* s);

    /* ── Bar series options (Phase 1B) ────────────────────────────────────── */

    void spectra_series_set_bar_width(SpectraSeries* s, float width);
    void spectra_series_set_bar_baseline(SpectraSeries* s, float baseline);
    void spectra_series_set_bar_orientation(SpectraSeries* s, int orientation);
    void spectra_series_set_bar_gradient(SpectraSeries* s, int enabled);

    /* ── Histogram series options (Phase 1C) ──────────────────────────────── */

    void spectra_series_set_histogram_bins(SpectraSeries* s, int bins);
    void spectra_series_set_histogram_cumulative(SpectraSeries* s, int enabled);
    void spectra_series_set_histogram_density(SpectraSeries* s, int enabled);
    void spectra_series_set_histogram_gradient(SpectraSeries* s, int enabled);

    /* ── 3D series (Phase 1D) ─────────────────────────────────────────────── */

    /* Add a 3D line series to a 3D axes. label can be NULL. */
    SpectraSeries* spectra_axes3d_line(SpectraAxes* ax,
                                       const float* x,
                                       const float* y,
                                       const float* z,
                                       uint32_t     count,
                                       const char*  label);

    /* Add a 3D scatter series to a 3D axes. label can be NULL. */
    SpectraSeries* spectra_axes3d_scatter(SpectraAxes* ax,
                                          const float* x,
                                          const float* y,
                                          const float* z,
                                          uint32_t     count,
                                          const char*  label);

    /* Add a 3D surface from grid vectors. x_grid has nx entries, y_grid has ny
     * entries, z_values is row-major with nx*ny entries. label can be NULL. */
    SpectraSeries* spectra_axes3d_surf(SpectraAxes* ax,
                                       const float* x_grid,
                                       uint32_t     nx,
                                       const float* y_grid,
                                       uint32_t     ny,
                                       const float* z_values,
                                       const char*  label);

    /* Update Z data for a 3D line/scatter series. */
    void spectra_series_set_z(SpectraSeries* s, const float* z, uint32_t count);

    /* Set colormap (SPECTRA_CMAP_*) — applies to surface series. */
    void spectra_series_set_colormap(SpectraSeries* s, int colormap);

    /* Set colormap value range — applies to surface series. */
    void spectra_series_set_colormap_range(SpectraSeries* s, float min_val, float max_val);

    /* ── Rendering ─────────────────────────────────────────────────────────── */

    /* Render one frame to RGBA buffer. Buffer must be width*height*4 bytes.
     * Returns 1 on success, 0 on failure. */
    int spectra_embed_render(SpectraEmbed* s, uint8_t* out_rgba);

    /* Resize the surface. Returns 1 on success, 0 on failure. */
    int spectra_embed_resize(SpectraEmbed* s, uint32_t width, uint32_t height);

    /* Render the current frame directly to a PNG file. Returns 1 on success. */
    int spectra_embed_render_png(SpectraEmbed* s, const char* path);

    /* Get current dimensions. */
    uint32_t spectra_embed_width(const SpectraEmbed* s);
    uint32_t spectra_embed_height(const SpectraEmbed* s);

    /* ── Input forwarding ──────────────────────────────────────────────────── */

    void spectra_embed_mouse_move(SpectraEmbed* s, float x, float y);
    void spectra_embed_mouse_button(SpectraEmbed* s,
                                    int           button,
                                    int           action,
                                    int           mods,
                                    float         x,
                                    float         y);
    void spectra_embed_scroll(SpectraEmbed* s, float dx, float dy, float cx, float cy);
    void spectra_embed_key(SpectraEmbed* s, int key, int action, int mods);

    /* Advance animations by dt seconds. */
    void spectra_embed_update(SpectraEmbed* s, float dt);

    /* Get/set background alpha (1.0 = opaque, 0.0 = transparent). */
    void  spectra_embed_set_background_alpha(SpectraEmbed* s, float alpha);
    float spectra_embed_get_background_alpha(const SpectraEmbed* s);

    /* ── Display configuration ────────────────────────────────────────────── */

    /* Set DPI scale factor (1.0 = 96 DPI, 2.0 = Retina/HiDPI).
     * Affects text size, tick length, and line widths. */
    void spectra_embed_set_dpi_scale(SpectraEmbed* s, float scale);

    /* Get current DPI scale factor. */
    float spectra_embed_get_dpi_scale(const SpectraEmbed* s);

    /* ── Theme & UI chrome ────────────────────────────────────────────────── */

    /* Set theme ("dark" or "light"). */
    void spectra_embed_set_theme(SpectraEmbed* s, const char* theme);

    /* Show/hide UI chrome elements (requires ImGui build).
     * State is always stored and applied to the live LayoutManager when present. */
    void spectra_embed_set_show_command_bar(SpectraEmbed* s, int visible);
    void spectra_embed_set_show_status_bar(SpectraEmbed* s, int visible);
    void spectra_embed_set_show_nav_rail(SpectraEmbed* s, int visible);
    void spectra_embed_set_show_inspector(SpectraEmbed* s, int visible);
    void spectra_embed_set_show_legend(SpectraEmbed* s, int visible);
    void spectra_embed_set_show_crosshair(SpectraEmbed* s, int visible);

    /* Query current UI chrome visibility (returns 0/1). */
    int spectra_embed_is_command_bar_visible(const SpectraEmbed* s);
    int spectra_embed_is_status_bar_visible(const SpectraEmbed* s);
    int spectra_embed_is_nav_rail_visible(const SpectraEmbed* s);
    int spectra_embed_is_inspector_visible(const SpectraEmbed* s);
    int spectra_embed_is_legend_visible(const SpectraEmbed* s);
    int spectra_embed_is_crosshair_visible(const SpectraEmbed* s);

    /* ── Animation & live data (Phase 4) ──────────────────────────────────── */

    /* Frame callback invoked inside spectra_embed_update(dt) with the elapsed
     * time and the delta of the current step. user_data is passed through. */
    typedef void (*SpectraFrameCb)(SpectraEmbed* s,
                                   float         time_sec,
                                   float         dt_sec,
                                   void*         user_data);
    void spectra_embed_set_on_frame(SpectraEmbed* s, SpectraFrameCb cb, void* user_data);
    void spectra_embed_clear_on_frame(SpectraEmbed* s);

    /* Redraw callback invoked when Spectra requests a repaint. */
    typedef void (*SpectraRedrawCb)(void* user_data);
    void spectra_embed_set_redraw_callback(SpectraEmbed* s, SpectraRedrawCb cb, void* user_data);

    /* Drive the frame callback over a fixed duration at the given fps.
     * Calls spectra_embed_update() internally; pass duration <= 0 for a single
     * step. Returns the number of frames stepped. */
    int spectra_embed_animation_play(SpectraEmbed* s, float fps, float duration_sec);
    /* Stop a running animation loop and clear the elapsed timeline. */
    void spectra_embed_animation_stop(SpectraEmbed* s);

    /* ── Interactive event callbacks (Phase 3) ────────────────────────────── */

    /* Fired when the user selects a concrete data point (left-click near it). */
    typedef void (*SpectraPointSelectedCb)(int    axes_index,
                                           int    series_index,
                                           size_t point_index,
                                           double x,
                                           double y,
                                           void*  user_data);
    /* Fired when the user selects a series (left-click near it). */
    typedef void (*SpectraSeriesSelectedCb)(int   axes_index,
                                            int   series_index,
                                            void* user_data);
    /* Fired when the hovered/nearest data point changes. series_index < 0 means
     * the cursor moved away from any series. */
    typedef void (*SpectraHoverCb)(int    axes_index,
                                   int    series_index,
                                   size_t point_index,
                                   double x,
                                   double y,
                                   void*  user_data);
    /* Fired when the visible data range (xlim/ylim) of the active axes changes. */
    typedef void (*SpectraViewChangedCb)(double xmin,
                                         double xmax,
                                         double ymin,
                                         double ymax,
                                         void*  user_data);

    /* Register interactive callbacks. Pass NULL cb to clear. Point/series/hover
     * callbacks require the ImGui chrome build (show_imgui_chrome). */
    void spectra_embed_set_on_point_selected(SpectraEmbed*          s,
                                             SpectraPointSelectedCb cb,
                                             void*                  user_data);
    void spectra_embed_set_on_series_selected(SpectraEmbed*           s,
                                              SpectraSeriesSelectedCb cb,
                                              void*                   user_data);
    void spectra_embed_set_on_hover(SpectraEmbed* s, SpectraHoverCb cb, void* user_data);
    void spectra_embed_set_on_view_changed(SpectraEmbed*        s,
                                           SpectraViewChangedCb cb,
                                           void*                user_data);

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

    /* Trigger auto-fit so axes limits are reset to encompass all data. */
    void spectra_axes_auto_fit(SpectraAxes* ax);

    /* ── Legend control (Phase 1E) ─────────────────────────────────────────── */

    /* Show/hide the legend on the figure that owns these axes. */
    void spectra_axes_show_legend(SpectraAxes* ax, int visible);
    /* Set legend position (SPECTRA_LEGEND_*). */
    void spectra_axes_set_legend_position(SpectraAxes* ax, int position);

    /* Add a histogram series. label can be NULL. Returns NULL on failure. */
    SpectraSeries* spectra_axes_histogram(SpectraAxes* ax,
                                          const float* values,
                                          uint32_t     count,
                                          int          bins,
                                          const char*  label);

    /* Add a bar series. label can be NULL. Returns NULL on failure. */
    SpectraSeries* spectra_axes_bar(SpectraAxes*  ax,
                                    const float*  positions,
                                    const float*  heights,
                                    uint32_t      count,
                                    const char*   label);

    /* ── Figure configuration ─────────────────────────────────────────────── */

    /* Set figure title. */
    void spectra_figure_set_title(SpectraFigure* fig, const char* title);

    /* ── Easy Render API ───────────────────────────────────────────────────── */
    /* One-call offscreen rendering. No surface/figure/axes management needed. */
    /* Caller must free returned buffer with spectra_free_pixels().            */

    /* Render a line plot to RGBA pixels. Returns pixel buffer (width*height*4).
     * out_width and out_height are set to the rendered dimensions.
     * Returns NULL on failure. Caller must call spectra_free_pixels(). */
    uint8_t* spectra_render_line(const float* x,
                                 const float* y,
                                 uint32_t     count,
                                 uint32_t     width,
                                 uint32_t     height,
                                 uint32_t*    out_width,
                                 uint32_t*    out_height);

    /* Render a scatter plot to RGBA pixels. Same semantics as spectra_render_line. */
    uint8_t* spectra_render_scatter(const float* x,
                                    const float* y,
                                    uint32_t     count,
                                    uint32_t     width,
                                    uint32_t     height,
                                    uint32_t*    out_width,
                                    uint32_t*    out_height);

    /* Render a line plot and save directly to PNG. Returns 1 on success. */
    int spectra_render_line_png(const float* x,
                                const float* y,
                                uint32_t     count,
                                uint32_t     width,
                                uint32_t     height,
                                const char*  path);

    /* Render a scatter plot and save directly to PNG. Returns 1 on success. */
    int spectra_render_scatter_png(const float* x,
                                   const float* y,
                                   uint32_t     count,
                                   uint32_t     width,
                                   uint32_t     height,
                                   const char*  path);

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
