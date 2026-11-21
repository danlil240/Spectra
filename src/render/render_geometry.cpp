// render_geometry.cpp — Grid, border, tick marks, screen-space overlay, plot text.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <vector>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series_image.hpp>
#include <spectra/series_shapes.hpp>

#include "core/axes3d.hpp"
#include "core/axes3d_box.hpp"
#include "core/axis_scale.hpp"
#include "core/axis_text_style.hpp"
#include "core/figure_layout.hpp"
#include "core/image_colormap.hpp"
#include "core/label_layout.hpp"
#include "text_fit.hpp"
#include "core/annotation_layout.hpp"
#include "ui/imgui/axes3d_renderer.hpp"
#include "ui/theme/theme.hpp"
#include "ui/viewmodel/axes_view_model.hpp"
#include "ui/viewmodel/figure_view_model.hpp"

namespace spectra
{

// Screen-space ortho projection (pixels, Y-down: 0=top, h=bottom) with identity
// view/model.  Vulkan clip space has Y-down (positive m[5]), WebGPU has Y-up.
static FrameUBO screen_space_ubo(float fw, float fh, bool clip_y_down)
{
    FrameUBO ubo{};   // Value-initializes all members to zero
    float    y_sign     = clip_y_down ? 1.0f : -1.0f;
    ubo.projection[0]   = 2.0f / fw;            // X: [0, fw] → [-1, +1]
    ubo.projection[5]   = y_sign * 2.0f / fh;   // Y: flip for WebGPU
    ubo.projection[10]  = -1.0f;
    ubo.projection[12]  = -1.0f;
    ubo.projection[13]  = y_sign * -1.0f;
    ubo.projection[15]  = 1.0f;
    ubo.view[0]         = 1.0f;
    ubo.view[5]         = 1.0f;
    ubo.view[10]        = 1.0f;
    ubo.view[15]        = 1.0f;
    ubo.model[0]        = 1.0f;
    ubo.model[5]        = 1.0f;
    ubo.model[10]       = 1.0f;
    ubo.model[15]       = 1.0f;
    ubo.viewport_width  = fw;
    ubo.viewport_height = fh;
    return ubo;
}

// Conditional sRGB-to-linear for themes with linearize_colors enabled.
static bool should_linearize_geom(const ui::ThemeManager& tm)
{
    return tm.current().linearize_colors;
}

static float srgb_chan_to_linear_geom(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static void set_pc_color_geom(float dst[4], const ui::Color& src, const ui::ThemeManager& tm)
{
    if (should_linearize_geom(tm))
    {
        dst[0] = srgb_chan_to_linear_geom(src.r);
        dst[1] = srgb_chan_to_linear_geom(src.g);
        dst[2] = srgb_chan_to_linear_geom(src.b);
    }
    else
    {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
    }
    dst[3] = src.a;
}

static void set_pc_color_geom(float                   dst[4],
                              const ui::Color&        src,
                              float                   alpha_override,
                              const ui::ThemeManager& tm)
{
    if (should_linearize_geom(tm))
    {
        dst[0] = srgb_chan_to_linear_geom(src.r);
        dst[1] = srgb_chan_to_linear_geom(src.g);
        dst[2] = srgb_chan_to_linear_geom(src.b);
    }
    else
    {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
    }
    dst[3] = alpha_override;
}

void Renderer::render_plot_text(Figure& figure)
{
    if (!text_renderer_.is_initialized())
        return;

    const auto& colors = plot_colors();

    auto color_to_rgba = [this](const ui::Color& c) -> uint32_t
    {
        float cr = c.r;
        float cg = c.g;
        float cb = c.b;
        if (should_linearize_geom(theme_mgr_))
        {
            cr = srgb_chan_to_linear_geom(cr);
            cg = srgb_chan_to_linear_geom(cg);
            cb = srgb_chan_to_linear_geom(cb);
        }
        auto r = static_cast<uint8_t>(cr * 255);
        auto g = static_cast<uint8_t>(cg * 255);
        auto b = static_cast<uint8_t>(cb * 255);
        auto a = static_cast<uint8_t>(c.a * 255);
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
               | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    };

    auto to_ui_color = [](const Color& c) { return ui::Color(c.r, c.g, c.b, c.a); };

    uint32_t tick_col  = color_to_rgba(colors.tick_label);
    uint32_t label_col = color_to_rgba(colors.text_secondary);
    uint32_t title_col = color_to_rgba(colors.text_primary);

    constexpr float tick_padding = 5.0f;

    // uint32_t fig_w = figure.width();
    uint32_t fig_h = figure.height();

    // Plot labels (title, axis labels, 3D title) are fitted by the rules shared with the
    // SVG exporter (core/label_layout): shrunk where allowed, then ellipsized.  Each
    // axes measures them at its own AxisStyle sizes (axes_label_measure below).
    const std::string_view label_ellipsis =
        text_renderer_.has_glyph(0x2026, FontSize::Label) ? LABEL_ELLIPSIS : "...";

    // ── Figure suptitle, centred above the subplot grid (rect from the layout) ──
    if (!figure.suptitle().empty())
    {
        // Scaled with the axes text (UI scale on screen), like the layout's band.
        const Rect& r   = figure.suptitle_rect();
        const float s   = SUPTITLE_SCALE * figure_text_scale(figure);
        const auto  ext = text_renderer_.measure_text(figure.suptitle(), FontSize::Title, s);
        if (r.w > 0.0f && r.y + ext.height >= 0.0f && r.y <= static_cast<float>(fig_h))
        {
            text_renderer_.draw_text(figure.suptitle(),
                                     r.x + r.w * 0.5f,
                                     r.y,
                                     FontSize::Title,
                                     title_col,
                                     TextAlign::Center,
                                     TextVAlign::Top,
                                     s);
        }
    }

    // ── 2D Axes: tick labels, axis labels, title ──
    // A twinx() twin follows its host, with its y axis text on the right.
    for (Axes* axes_ptr : axes_with_twins(figure))
    {
        auto&       axes    = *axes_ptr;
        const auto& vp      = axes.viewport();
        const bool  right_y = axes.is_twin();

        // Skip axes entirely outside the framebuffer (scrolled off-screen)
        if (vp.y + vp.h < 0.0f || vp.y > static_cast<float>(fig_h))
            continue;

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        {
            auto* axes2d_ptr = dynamic_cast<Axes*>(&axes);
            if (figure_vm_ && axes2d_ptr)
            {
                auto& avm = figure_vm_->get_or_create_axes_vm(axes2d_ptr);
                xlim      = avm.visual_xlim();
                ylim      = avm.visual_ylim();
            }
            else
            {
                xlim = axes.x_limits();
                ylim = axes.y_limits();
            }
        }

        // Map through axis space so log axes place ticks/grid in decades.
        const auto*      scale_axes = dynamic_cast<const Axes*>(&axes);
        const ScaleType  xscale     = scale_axes ? scale_axes->xscale() : ScaleType::Linear;
        const ScaleType  yscale     = scale_axes ? scale_axes->yscale() : ScaleType::Linear;
        const AxisLimits ax_x       = axis_space_limits(xscale, xlim);
        const AxisLimits ax_y       = axis_space_limits(yscale, ylim);

        double x_range = ax_x.max - ax_x.min;
        double y_range = ax_y.max - ax_y.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float {
            return static_cast<float>(vp.x
                                      + (to_axis_space(xscale, dx) - ax_x.min) / x_range * vp.w);
        };
        auto data_to_px_y = [&](double dy) -> float
        {
            return static_cast<float>(
                vp.y + (1.0 - (to_axis_space(yscale, dy) - ax_y.min) / y_range) * vp.h);
        };

        // AxisStyle font sizes / colours (unset → theme) and the axes' text
        // scale (UI scale on screen), shared with the layout and the SVG/PDF
        // exporter (core/axis_text_style).
        const auto&           as  = axes.axis_style();
        const AxisTextMetrics tm  = axis_text_metrics(axes);
        const float           tl  = tm.tick_length;
        const float           pad = tm.px(tick_padding);
        const auto            tf  = TextRenderer::scaled_font(tm.tick_px);
        const uint32_t        ax_tick_col =
            axis_color_is_set(as.tick_color) ? color_to_rgba(to_ui_color(as.tick_color)) : tick_col;
        const uint32_t     ax_label_col       = axis_color_is_set(as.label_color)
                                                    ? color_to_rgba(to_ui_color(as.label_color))
                                                    : label_col;
        const LabelWidthFn axes_label_measure = [&](const std::string& t, LabelFont f)
        {
            const auto sf = TextRenderer::scaled_font(tm.font_px(f));
            return text_renderer_.measure_text(t, sf.preset, sf.scale).width;
        };
        // The title is fitted at sizes no larger than its own (AxisTextMetrics::title_font_px).
        const LabelWidthFn title_measure = [&](const std::string& t, LabelFont f)
        {
            const auto sf = TextRenderer::scaled_font(tm.title_font_px(f));
            return text_renderer_.measure_text(t, sf.preset, sf.scale).width;
        };

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();
        // Inner axes of a shared column / row draw no tick labels (figure layout).
        if (!axes.x_tick_labels_visible())
        {
            x_ticks.labels.clear();
            x_ticks.positions.clear();
            x_ticks.offset_label.clear();
        }
        if (!axes.y_tick_labels_visible())
        {
            y_ticks.labels.clear();
            y_ticks.positions.clear();
            y_ticks.offset_label.clear();
        }

        // X tick labels — skip labels that would overlap
        {
            float       last_x_right = -1e30f;
            const float label_gap    = tm.px(6.0f);
            for (size_t i = 0; i < x_ticks.positions.size(); ++i)
            {
                float px     = data_to_px_x(x_ticks.positions[i]);
                auto  ext    = text_renderer_.measure_text(x_ticks.labels[i], tf.preset, tf.scale);
                float x_left = px - ext.width * 0.5f;
                if (x_left < last_x_right + label_gap && i > 0)
                    continue;
                text_renderer_.draw_text(x_ticks.labels[i],
                                         px,
                                         vp.y + vp.h + tl + pad,
                                         tf.preset,
                                         ax_tick_col,
                                         TextAlign::Center,
                                         TextVAlign::Top,
                                         tf.scale);
                last_x_right = px + ext.width * 0.5f;
            }
        }

        // Y tick labels — skip labels that would overlap.
        // Ticks are in ascending data order = ascending screen-Y bottom-to-top.
        // Iterate in reverse (high data → low data = top-to-bottom on screen).
        // Track last_y_bottom: skip next label if its top is within gap of last bottom.
        float max_y_tick_width = 0.0f;
        {
            float       last_y_bottom = -1e30f;
            const float label_gap     = tm.px(4.0f);
            for (int i = static_cast<int>(y_ticks.positions.size()) - 1; i >= 0; --i)
            {
                float py    = data_to_px_y(y_ticks.positions[i]);
                auto  ext   = text_renderer_.measure_text(y_ticks.labels[i], tf.preset, tf.scale);
                float y_top = py - ext.height * 0.5f;
                float y_bottom = py + ext.height * 0.5f;
                // Skip if this label's top is above the bottom of the last drawn label
                // (i.e. would overlap going downward).
                // Always draw the topmost tick (i == size-1, first in this loop).
                if (y_top < last_y_bottom + label_gap
                    && i < static_cast<int>(y_ticks.positions.size()) - 1)
                    continue;
                text_renderer_.draw_text(y_ticks.labels[i],
                                         right_y ? vp.x + vp.w + tl + pad : vp.x - tl - pad,
                                         py,
                                         tf.preset,
                                         ax_tick_col,
                                         right_y ? TextAlign::Left : TextAlign::Right,
                                         TextVAlign::Middle,
                                         tf.scale);
                last_y_bottom    = y_bottom;
                max_y_tick_width = std::max(max_y_tick_width, ext.width);
            }
        }

        // Shared offset / multiplier of the tick labels ("×10⁻⁶", "+1.7×10⁹"):
        // x below the right end of the tick labels, y above the top of the axis.
        const float tick_text_h = text_renderer_.measure_text("0", tf.preset, tf.scale).height;
        const float x_label_row = vp.y + vp.h + tl + pad + tick_text_h + tm.px(4.0f);
        // The offsets share their rows with the X label / title: keep their width free so
        // the centred labels are fitted beside them instead of drawn over them.
        float x_offset_reserve = 0.0f;
        float y_offset_reserve = 0.0f;
        if (!x_ticks.offset_label.empty())
        {
            x_offset_reserve = offset_annotation_reserve(
                text_renderer_.measure_text(x_ticks.offset_label, tf.preset, tf.scale).width);
            text_renderer_.draw_text(x_ticks.offset_label,
                                     vp.x + vp.w,
                                     x_label_row,
                                     tf.preset,
                                     ax_tick_col,
                                     TextAlign::Right,
                                     TextVAlign::Top,
                                     tf.scale);
        }
        if (!y_ticks.offset_label.empty())
        {
            y_offset_reserve = offset_annotation_reserve(
                text_renderer_.measure_text(y_ticks.offset_label, tf.preset, tf.scale).width);
            text_renderer_.draw_text(y_ticks.offset_label,
                                     right_y ? vp.x + vp.w : vp.x,
                                     vp.y - tm.px(4.0f),
                                     tf.preset,
                                     ax_tick_col,
                                     right_y ? TextAlign::Right : TextAlign::Left,
                                     TextVAlign::Bottom,
                                     tf.scale);
        }

        // A fitted label's font: its tier's AxisStyle size times the fit's shrink factor.
        auto label_font = [&](const FittedLabel& fit)
        {
            auto sf = TextRenderer::scaled_font(tm.font_px(fit.font));
            sf.scale *= fit.scale;
            return sf;
        };

        // X axis label (ellipsized to the axes width left of the X offset)
        if (!axes.get_xlabel().empty())
        {
            const CenteredLabelSlot slot = centered_label_slot(vp.x, vp.w, 0.0f, x_offset_reserve);
            const FittedLabel       fit  = fit_axes_xlabel(axes.get_xlabel(),
                                                    vp.w,
                                                    x_offset_reserve,
                                                    axes_label_measure,
                                                    label_ellipsis);
            const auto              sf   = label_font(fit);
            const auto ext = text_renderer_.measure_text(fit.text, sf.preset, sf.scale);
            float      cx  = centered_label_x(vp.x, vp.w, slot, ext.width);
            float      py  = x_label_row;
            text_renderer_.draw_text(fit.text,
                                     cx,
                                     py,
                                     sf.preset,
                                     ax_label_col,
                                     TextAlign::Center,
                                     TextVAlign::Top,
                                     sf.scale);
            if (fit.truncated)
            {
                truncated_text_regions_.push_back(
                    {Rect{cx - ext.width * 0.5f, py, ext.width, ext.height}, axes.get_xlabel()});
            }
        }

        // Y axis label (rotated -90°)
        if (!axes.get_ylabel().empty())
        {
            // Rotated: the label runs along the axes height, so fit it to that.
            const FittedLabel fit =
                fit_axes_ylabel(axes.get_ylabel(), vp.h, axes_label_measure, label_ellipsis);
            const auto  sf        = label_font(fit);
            const auto  label_ext = text_renderer_.measure_text(fit.text, sf.preset, sf.scale);
            const float label_gap = tm.px(6.0f);   // gap between widest tick label and axis label
            const float out = tl + pad + max_y_tick_width + label_gap + label_ext.height * 0.5f;
            float       center_x       = right_y ? vp.x + vp.w + out : vp.x - out;
            float       center_y       = vp.y + vp.h * 0.5f;
            constexpr float neg_90_deg = -1.5707963f;   // -π/2
            text_renderer_.draw_text_rotated(fit.text,
                                             center_x,
                                             center_y,
                                             neg_90_deg,
                                             sf.preset,
                                             ax_label_col,
                                             TextAlign::Center,
                                             TextVAlign::Middle,
                                             sf.scale);
            if (fit.truncated)
            {
                truncated_text_regions_.push_back({Rect{center_x - label_ext.height * 0.5f,
                                                        center_y - label_ext.width * 0.5f,
                                                        label_ext.height,
                                                        label_ext.width},
                                                   axes.get_ylabel()});
            }
        }

        // Title
        if (!axes.get_title().empty())
        {
            // Long titles shrink smoothly to the label size, then get ellipsized, so they
            // never spill into neighbouring subplots or past the window edge.
            // The Y offset sits at the top-left on the title's row: fit beside it.
            const CenteredLabelSlot slot = centered_label_slot(vp.x, vp.w, y_offset_reserve, 0.0f);
            const FittedLabel       fit  = fit_axes_title(axes.get_title(),
                                                   vp.w,
                                                   y_offset_reserve,
                                                   title_measure,
                                                   label_ellipsis);
            auto                    sf   = TextRenderer::scaled_font(tm.title_font_px(fit.font));
            sf.scale *= fit.scale;
            auto  ext = text_renderer_.measure_text(fit.text, sf.preset, sf.scale);
            float cx  = centered_label_x(vp.x, vp.w, slot, ext.width);
            float py  = vp.y - ext.height - pad;
            // Only clamp if the title would go off the top of the figure.
            // The previous clamp (py < vp.y + 2) always fired and placed the
            // title inside the axes viewport where it competed with grid/series
            // rendering, contributing to flicker in split-view panes.
            if (py < 2.0f)
                py = 2.0f;
            text_renderer_.draw_text(fit.text,
                                     cx,
                                     py,
                                     sf.preset,
                                     title_col,
                                     TextAlign::Center,
                                     TextVAlign::Top,
                                     sf.scale);
            if (fit.truncated)
            {
                truncated_text_regions_.push_back(
                    {Rect{cx - ext.width * 0.5f, py, ext.width, ext.height}, axes.get_title()});
            }
        }

        // ── Heatmap cell values (ImageSeries::show_values), clipped to the plot area ──
        if (vp.w > 0.0f && vp.h > 0.0f)
        {
            bool clipped = false;
            for (const auto& sp : axes.series())
            {
                const auto* img = dynamic_cast<const ImageSeries*>(sp.get());
                if (!img || !img->visible() || !img->show_values() || !img->has_colormap())
                    continue;
                constexpr size_t MAX_LABELLED_CELLS = 40000;
                if (img->cols() * img->rows() > MAX_LABELLED_CELLS)
                    continue;
                const float cell_w = std::abs(data_to_px_x(img->x1()) - data_to_px_x(img->x0()))
                                     / static_cast<float>(img->cols());
                const float cell_h = std::abs(data_to_px_y(img->y1()) - data_to_px_y(img->y0()))
                                     / static_cast<float>(img->rows());
                const float row_h = text_renderer_.measure_text("0", FontSize::Tick).height;
                if (!(cell_w >= 12.0f) || !(cell_h >= row_h + 2.0f))
                    continue;   // cells too small to hold a label
                if (!clipped)
                {
                    text_renderer_.set_clip_rect(vp.x, vp.y, vp.x + vp.w, vp.y + vp.h);
                    clipped = true;
                }
                const auto lut  = image_colormap::build_lut(img->cmap(), img->levels());
                const auto vals = img->data();
                for (size_t r = 0; r < img->rows(); ++r)
                {
                    for (size_t c = 0; c < img->cols(); ++c)
                    {
                        const float v = vals[r * img->cols() + c];
                        if (!std::isfinite(v))
                            continue;
                        double cx = 0.0;
                        double cy = 0.0;
                        img->cell_center(c, r, cx, cy);
                        const float px = data_to_px_x(cx);
                        const float py = data_to_px_y(cy);
                        if (!(px >= vp.x && px <= vp.x + vp.w && py >= vp.y && py <= vp.y + vp.h))
                            continue;
                        const std::string text = std::format("{:.{}f}", v, img->value_decimals());
                        if (text_renderer_.measure_text(text, FontSize::Tick).width > cell_w - 2.0f)
                            continue;
                        // Dark text on light cells, light text on dark cells.
                        const float t = image_colormap::normalized(v, img->vmin(), img->vmax());
                        const Color cc =
                            image_colormap::lut_color(lut,
                                                      image_colormap::lut_index(t, lut.size() / 4));
                        const float    lum   = 0.2126f * cc.r + 0.7152f * cc.g + 0.0722f * cc.b;
                        const uint32_t trgba = lum > 0.5f ? 0xFF1A1A1Au : 0xFFFFFFFFu;
                        text_renderer_.draw_text(text,
                                                 px,
                                                 py,
                                                 FontSize::Tick,
                                                 trgba,
                                                 TextAlign::Center,
                                                 TextVAlign::Middle);
                    }
                }
            }
            if (clipped)
                text_renderer_.clear_clip_rect();
        }

        // ── ShapeSeries text annotations (data-space text labels) ──
        for (const auto& sp : axes.series())
        {
            auto* shape = dynamic_cast<const ShapeSeries*>(sp.get());
            if (!shape || shape->text_annotations().empty())
                continue;

            const auto& sc = shape->color();
            for (const auto& ann : shape->text_annotations())
            {
                float px = data_to_px_x(ann.x);
                float py = data_to_px_y(ann.y);

                // Determine text color: use annotation color if set, else series color
                Color    tc        = (ann.text_color.a > 0.01f) ? ann.text_color : sc;
                auto     tr        = static_cast<uint8_t>(tc.r * 255);
                auto     tg        = static_cast<uint8_t>(tc.g * 255);
                auto     tb        = static_cast<uint8_t>(tc.b * 255);
                auto     ta        = static_cast<uint8_t>(tc.a * shape->opacity() * 255);
                uint32_t text_rgba = static_cast<uint32_t>(tr) | (static_cast<uint32_t>(tg) << 8)
                                     | (static_cast<uint32_t>(tb) << 16)
                                     | (static_cast<uint32_t>(ta) << 24);

                // Explicit sizes are drawn at exactly that pixel size; 0 = Label.
                const auto sf        = TextRenderer::scaled_font(ann.font_size);
                auto       ta_align  = static_cast<TextAlign>(std::clamp(ann.align, 0, 2));
                auto       ta_valign = static_cast<TextVAlign>(std::clamp(ann.valign, 0, 2));

                text_renderer_.draw_text(ann.content,
                                         px,
                                         py,
                                         sf.preset,
                                         text_rgba,
                                         ta_align,
                                         ta_valign,
                                         sf.scale);
            }
        }

        // ── Axes text annotations (Axes::text / Axes::annotate), clipped to the plot area ──
        if (!axes.annotations().empty() && vp.w > 0.0f && vp.h > 0.0f)
        {
            const uint32_t default_rgba = color_to_rgba(colors.text_primary);
            text_renderer_.set_clip_rect(vp.x, vp.y, vp.x + vp.w, vp.y + vp.h);
            for (const auto& ann : axes.annotations())
            {
                if (!ann || !ann->visible() || ann->text().empty())
                    continue;
                float px = data_to_px_x(ann->x());
                float py = data_to_px_y(ann->y());
                if (!std::isfinite(px) || !std::isfinite(py))
                    continue;

                uint32_t rgba = default_rgba;
                if (ann->color())
                {
                    const Color& c = *ann->color();
                    rgba           = static_cast<uint32_t>(c.r * 255.0f)
                           | (static_cast<uint32_t>(c.g * 255.0f) << 8)
                           | (static_cast<uint32_t>(c.b * 255.0f) << 16)
                           | (static_cast<uint32_t>(c.a * 255.0f) << 24);
                }
                // Drawn at exactly font_size() pixels (a baked preset scaled), not
                // snapped to the nearest preset; 0 keeps the Label preset.
                const auto sf     = TextRenderer::scaled_font(ann->font_size());
                const auto halign = static_cast<TextAlign>(ann->halign());
                const auto valign = static_cast<TextVAlign>(ann->valign());
                // Rotation is counter-clockwise; the text renderer turns clockwise.
                const float angle = -ann->rotation() * 3.14159265358979f / 180.0f;
                auto        draw  = [&](float x, float y, uint32_t c)
                {
                    if (ann->rotation() == 0.0f)
                        text_renderer_
                            .draw_text(ann->text(), x, y, sf.preset, c, halign, valign, sf.scale);
                    else
                        text_renderer_.draw_text_rotated(ann->text(),
                                                         x,
                                                         y,
                                                         angle,
                                                         sf.preset,
                                                         c,
                                                         halign,
                                                         valign,
                                                         sf.scale);
                };
                // Halo: copies of the text in the halo colour around it, under the text.
                if (ann->halo() && ann->halo_width() > 0.0f)
                {
                    uint32_t halo_rgba = color_to_rgba(colors.bg_canvas);
                    if (const auto& hc = ann->halo_color())
                        halo_rgba = static_cast<uint32_t>(hc->r * 255.0f)
                                    | (static_cast<uint32_t>(hc->g * 255.0f) << 8)
                                    | (static_cast<uint32_t>(hc->b * 255.0f) << 16)
                                    | (static_cast<uint32_t>(hc->a * 255.0f) << 24);
                    for (int k = 0; k < annotation_layout::HALO_COPIES; ++k)
                        draw(px + annotation_layout::HALO_DIR_X[k] * ann->halo_width(),
                             py + annotation_layout::HALO_DIR_Y[k] * ann->halo_width(),
                             halo_rgba);
                }
                draw(px, py, rgba);
            }
            text_renderer_.clear_clip_rect();
        }
    }

    // ── 3D Axes: billboarded tick labels, axis labels, title ──
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const auto& vp  = axes3d->viewport();
        const auto& cam = axes3d->camera();

        // AxisStyle sizes / tick colour and the UI text scale, as for 2D axes.
        const AxisTextMetrics tm3            = axis_text_metrics(*axes3d);
        const auto            tf3            = TextRenderer::scaled_font(tm3.tick_px);
        const auto            lf3            = TextRenderer::scaled_font(tm3.label_px);
        const float           pad3           = tm3.px(tick_padding);
        const uint32_t        tick_col3      = axis_color_is_set(axes3d->axis_style().tick_color)
                                                   ? color_to_rgba(to_ui_color(axes3d->axis_style().tick_color))
                                                   : tick_col;
        const LabelWidthFn    title3_measure = [&](const std::string& t, LabelFont f)
        {
            const auto sf = TextRenderer::scaled_font(tm3.title_font_px(f));
            return text_renderer_.measure_text(t, sf.preset, sf.scale).width;
        };

        // Build MVP matrix: projection * view * model
        mat4 proj  = axes3d->projection_matrix(vp);
        mat4 view  = cam.view_matrix();
        mat4 model = axes3d->data_to_normalized_matrix();
        mat4 mvp   = mat4_mul(proj, mat4_mul(view, model));

        // Helper: project a 3D world point to screen coords within the viewport
        // Also outputs NDC depth in [0,1] for depth-tested text rendering.
        auto world_to_screen = [&](vec3 world_pos, float& sx, float& sy, float& ndc_depth) -> bool
        {
            float clip_x = mvp.m[0] * world_pos.x + mvp.m[4] * world_pos.y + mvp.m[8] * world_pos.z
                           + mvp.m[12];
            float clip_y = mvp.m[1] * world_pos.x + mvp.m[5] * world_pos.y + mvp.m[9] * world_pos.z
                           + mvp.m[13];
            float clip_z = mvp.m[2] * world_pos.x + mvp.m[6] * world_pos.y + mvp.m[10] * world_pos.z
                           + mvp.m[14];
            float clip_w = mvp.m[3] * world_pos.x + mvp.m[7] * world_pos.y + mvp.m[11] * world_pos.z
                           + mvp.m[15];

            if (clip_w <= 0.001f)
                return false;

            float ndc_x = clip_x / clip_w;
            float ndc_y = clip_y / clip_w;
            ndc_depth   = clip_z / clip_w;   // Vulkan depth range [0,1]

            sx = vp.x + (ndc_x + 1.0f) * 0.5f * vp.w;
            sy = vp.y + (ndc_y + 1.0f) * 0.5f * vp.h;

            float margin = 200.0f;
            return sx >= vp.x - margin && sx <= vp.x + vp.w + margin && sy >= vp.y - margin
                   && sy <= vp.y + vp.h + margin;
        };

        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();

        auto x0 = static_cast<float>(xlim.min);
        auto y0 = static_cast<float>(ylim.min);
        auto z0 = static_cast<float>(zlim.min);

        vec3 view_dir = vec3_normalize(cam.target - cam.position);

        // Tick-label placement follows the camera's up axis (Z by default):
        // the two horizontal axes are labelled along the bottom box edge
        // nearest the camera, the vertical axis along the leftmost vertical
        // edge on screen -- so labels sit outside the data, as in MATLAB.
        const int   up_idx    = (cam.up_axis == Camera::UpAxis::Z) ? 2 : 1;
        const float lo_lim[3] = {x0, y0, z0};
        const float hi_lim[3] = {static_cast<float>(xlim.max),
                                 static_cast<float>(ylim.max),
                                 static_cast<float>(zlim.max)};
        const float view_c[3] = {static_cast<float>(view_dir.x),
                                 static_cast<float>(view_dir.y),
                                 static_cast<float>(view_dir.z)};
        float       tick_off[3];
        for (int i = 0; i < 3; ++i)
            tick_off[i] = (hi_lim[i] - lo_lim[i]) * 0.04f;
        // Same faces as the grid planes and box edges (core/axes3d_box.hpp):
        // labels go on the near side of the back panes, the vertical axis on
        // the leftmost bottom corner on screen.
        const axes3d_box::BoxFaces faces     = axes3d_box::compute_faces(*axes3d);
        auto                       near_edge = [&](int axis) -> float
        {
            return faces.back_hi[static_cast<size_t>(axis)] ? lo_lim[axis] - tick_off[axis]
                                                            : hi_lim[axis] + tick_off[axis];
        };
        const int h_a            = 0;   // X is horizontal for both Z-up and Y-up
        const int h_b            = (up_idx == 2) ? 1 : 2;
        float     vert_corner[3] = {x0, y0, z0};
        for (int i : {h_a, h_b})
            vert_corner[i] = faces.up_corner_hi[static_cast<size_t>(i)] ? hi_lim[i] : lo_lim[i];
        auto tick_anchor = [&](int axis, float value) -> vec3
        {
            float p[3];
            p[axis] = value;
            if (axis == up_idx)
            {
                p[h_a] = vert_corner[h_a];
                p[h_b] = vert_corner[h_b];
            }
            else
            {
                const int other = 3 - axis - up_idx;
                p[other]        = near_edge(other);
                p[up_idx]       = lo_lim[up_idx];
            }
            return {p[0], p[1], p[2]};
        };
        constexpr float tick_label_min_spacing_px = 18.0f;
        auto            should_skip_overlapping_tick =
            [&](float sx, float sy, float& last_sx, float& last_sy, bool& has_last) -> bool
        {
            if (!has_last)
            {
                last_sx  = sx;
                last_sy  = sy;
                has_last = true;
                return false;
            }
            float dx = sx - last_sx;
            float dy = sy - last_sy;
            if ((dx * dx + dy * dy) < (tick_label_min_spacing_px * tick_label_min_spacing_px))
                return true;
            last_sx = sx;
            last_sy = sy;
            return false;
        };

        // Screen rectangles of every label placed so far (title, ticks of all
        // three axes, arrow labels).  The three axes' tick labels all meet at
        // the origin corner, so per-axis spacing alone lets e.g. the X and Y
        // "0" labels print on top of each other ("00").
        struct LabelRect
        {
            float x0, y0, x1, y1;
        };
        std::vector<LabelRect> placed_labels;
        auto                   label_rect = [&](const std::string& text,
                              FontSize           size,
                              float              x,
                              float              y,
                              TextAlign          align,
                              TextVAlign         valign,
                              float              scale = 1.0f) -> LabelRect
        {
            const auto ext = text_renderer_.measure_text(text, size, scale);
            float      lx  = x;
            if (align == TextAlign::Center)
                lx -= ext.width * 0.5f;
            else if (align == TextAlign::Right)
                lx -= ext.width;
            float ty = y;
            if (valign == TextVAlign::Middle)
                ty -= ext.height * 0.5f;
            else if (valign == TextVAlign::Bottom)
                ty -= ext.height;
            return {lx, ty, lx + ext.width, ty + ext.height};
        };
        auto collides = [&](const LabelRect& r) -> bool
        {
            constexpr float gap = 2.0f;
            for (const auto& o : placed_labels)
            {
                if (r.x0 < o.x1 + gap && r.x1 + gap > o.x0 && r.y0 < o.y1 + gap
                    && r.y1 + gap > o.y0)
                    return true;
            }
            return false;
        };
        // Labels may use the layout margins of this subplot's cell (like 2D
        // tick labels), but 3D text is not scissored, so a label sticking out
        // of the cell would print over a neighbouring subplot; drop it instead.
        const Rect cell             = axes3d_cell_rect(vp,
                                           figure.style(),
                                           static_cast<float>(figure.width()),
                                           static_cast<float>(figure.height()),
                                           tm3.scale);
        auto       outside_viewport = [&](const LabelRect& r) -> bool
        {
            constexpr float tol = 1.0f;
            return r.x0 < cell.x - tol || r.x1 > cell.x + cell.w + tol || r.y0 < cell.y - tol
                   || r.y1 > cell.y + cell.h + tol;
        };
        // Draw a depth-tested tick label unless it would overlap another label.
        auto place_tick_label = [&](const std::string& text,
                                    float              x,
                                    float              y,
                                    float              depth,
                                    TextAlign          align,
                                    TextVAlign         valign)
        {
            const LabelRect r = label_rect(text, tf3.preset, x, y, align, valign, tf3.scale);
            if (collides(r) || outside_viewport(r))
                return;
            placed_labels.push_back(r);
            text_renderer_.draw_text_depth(text,
                                           x,
                                           y,
                                           depth,
                                           tf3.preset,
                                           tick_col3,
                                           align,
                                           valign,
                                           tf3.scale);
        };

        // Reserve the title's rectangle first so nothing is drawn over it.
        float      title_py = 0.0f;
        FittedText title_fit;
        if (!axes3d->get_title().empty())
        {
            const FittedLabel fit3 =
                fit_axes3d_title(axes3d->get_title(), vp.w, title3_measure, label_ellipsis);
            const auto sf3      = TextRenderer::scaled_font(tm3.title_font_px(fit3.font));
            title_fit.text      = fit3.text;
            title_fit.size      = sf3.preset;
            title_fit.scale     = sf3.scale * fit3.scale;
            title_fit.truncated = fit3.truncated;
            auto ext = text_renderer_.measure_text(title_fit.text, title_fit.size, title_fit.scale);
            // Same row as a 2D title (2D pass above): above the viewport, in the
            // margin the layout reserves for it, so 2D and 3D subplot titles line up.
            // Clamp only at the top of the figure.
            title_py = vp.y - ext.height - pad3;
            if (title_py < 2.0f)
                title_py = 2.0f;
            placed_labels.push_back(label_rect(title_fit.text,
                                               title_fit.size,
                                               vp.x + vp.w * 0.5f,
                                               title_py,
                                               TextAlign::Center,
                                               TextVAlign::Top,
                                               title_fit.scale));
            if (title_fit.truncated)
            {
                truncated_text_regions_.push_back(
                    {Rect{vp.x + vp.w * 0.5f - ext.width * 0.5f, title_py, ext.width, ext.height},
                     axes3d->get_title()});
            }
        }

        // --- Tick labels for X, Y, Z ---
        for (int axis = 0; axis < 3; ++axis)
        {
            // Looking straight down an axis collapses its ticks onto a point.
            if (std::abs(view_c[axis]) > 0.98f)
                continue;
            const auto ticks    = axis == 0   ? axes3d->compute_x_ticks()
                                  : axis == 1 ? axes3d->compute_y_ticks()
                                              : axes3d->compute_z_ticks();
            const bool vertical = (axis == up_idx);
            float      last_sx  = 0.0f;
            float      last_sy  = 0.0f;
            bool       has_last = false;
            // The two horizontal tick edges meet at the front corner, where both
            // axes may label their end value ("2" next to "-2" read as "-2 2").
            // The second horizontal axis leaves that corner to the first.
            const bool  skip_corner = (axis == h_b);
            const float corner_v =
                faces.back_hi[static_cast<size_t>(axis)] ? lo_lim[axis] : hi_lim[axis];
            const float corner_tol = (hi_lim[axis] - lo_lim[axis]) * 0.1f;
            for (size_t i = 0; i < ticks.positions.size(); ++i)
            {
                if (skip_corner
                    && std::abs(static_cast<float>(ticks.positions[i]) - corner_v) <= corner_tol)
                    continue;
                float      sx    = 0.0f;
                float      sy    = 0.0f;
                float      depth = 0.0f;
                const vec3 pos   = tick_anchor(axis, static_cast<float>(ticks.positions[i]));
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                if (vertical)
                    place_tick_label(ticks.labels[i],
                                     sx - pad3,
                                     sy,
                                     depth,
                                     TextAlign::Right,
                                     TextVAlign::Middle);
                else
                    place_tick_label(ticks.labels[i],
                                     sx,
                                     sy,
                                     depth,
                                     TextAlign::Center,
                                     TextVAlign::Top);
            }
        }

        // --- 3D axis arrow labels ---
        {
            auto x1          = static_cast<float>(xlim.max);
            auto y1          = static_cast<float>(ylim.max);
            auto z1          = static_cast<float>(zlim.max);
            auto arrow_len_x = static_cast<float>((xlim.max - xlim.min) * 0.18);
            auto arrow_len_y = static_cast<float>((ylim.max - ylim.min) * 0.18);
            auto arrow_len_z = static_cast<float>((zlim.max - zlim.min) * 0.18);

            auto pack_rgba = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t
            {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
                       | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };
            uint32_t vk_x_arrow_col = pack_rgba(230, 70, 70, 220);
            uint32_t vk_y_arrow_col = pack_rgba(70, 200, 70, 220);
            uint32_t vk_z_arrow_col = pack_rgba(80, 130, 255, 220);

            // Helper: draw arrow label text at the tip of an axis arrow
            auto draw_arrow_label =
                [&](vec3 start, vec3 end, uint32_t vk_col, const std::string& lbl)
            {
                float sx0 = 0.0f;
                float sy0 = 0.0f;
                float d0  = 0.0f;
                float sx1 = 0.0f;
                float sy1 = 0.0f;
                float d1  = 0.0f;
                if (!world_to_screen(start, sx0, sy0, d0) || !world_to_screen(end, sx1, sy1, d1))
                    return;
                float label_offset = 8.0f;
                float dir_x        = sx1 - sx0;
                float dir_y        = sy1 - sy0;
                float dir_len      = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                float lx = sx1 + (dir_len > 1.0f ? dir_x / dir_len * label_offset : label_offset);
                float ly_center = sy1 + (dir_len > 1.0f ? dir_y / dir_len * label_offset : 0.0f);
                // If the label would land on the title (typical for the
                // vertical Y arrow), move it beside the arrow tip, below the title.
                LabelRect r = label_rect(lbl,
                                         lf3.preset,
                                         lx,
                                         ly_center,
                                         TextAlign::Left,
                                         TextVAlign::Middle,
                                         lf3.scale);
                if (collides(r))
                {
                    lx          = sx1 + label_offset;
                    float shift = 0.0f;
                    for (const auto& o : placed_labels)
                    {
                        LabelRect moved = label_rect(lbl,
                                                     lf3.preset,
                                                     lx,
                                                     ly_center + shift,
                                                     TextAlign::Left,
                                                     TextVAlign::Middle,
                                                     lf3.scale);
                        if (moved.x0 < o.x1 && moved.x1 > o.x0 && moved.y0 < o.y1
                            && moved.y1 > o.y0)
                            shift += o.y1 - moved.y0 + 2.0f;
                    }
                    ly_center += shift;
                    r = label_rect(lbl,
                                   lf3.preset,
                                   lx,
                                   ly_center,
                                   TextAlign::Left,
                                   TextVAlign::Middle,
                                   lf3.scale);
                }
                if (outside_viewport(r))
                    return;
                placed_labels.push_back(r);
                text_renderer_.draw_text_depth(lbl,
                                               lx,
                                               ly_center,
                                               d1,
                                               lf3.preset,
                                               vk_col,
                                               TextAlign::Left,
                                               TextVAlign::Middle,
                                               lf3.scale);
            };

            draw_arrow_label({x1, y0, z0},
                             {x1 + arrow_len_x, y0, z0},
                             vk_x_arrow_col,
                             axes3d_arrow_label(axes3d->get_xlabel(),
                                                "X",
                                                axes3d->compute_x_ticks().offset_label));
            draw_arrow_label({x0, y1, z0},
                             {x0, y1 + arrow_len_y, z0},
                             vk_y_arrow_col,
                             axes3d_arrow_label(axes3d->get_ylabel(),
                                                "Y",
                                                axes3d->compute_y_ticks().offset_label));
            draw_arrow_label({x0, y0, z1},
                             {x0, y0, z1 + arrow_len_z},
                             vk_z_arrow_col,
                             axes3d_arrow_label(axes3d->get_zlabel(),
                                                "Z",
                                                axes3d->compute_z_ticks().offset_label));
        }

        // --- 3D Title --- (rectangle reserved above, before the tick labels)
        if (!title_fit.text.empty())
        {
            float cx = vp.x + vp.w * 0.5f;
            text_renderer_.draw_text(title_fit.text,
                                     cx,
                                     title_py,
                                     title_fit.size,
                                     title_col,
                                     TextAlign::Center,
                                     TextVAlign::Top,
                                     title_fit.scale);
        }
    }
}

void Renderer::bind_screen_space_ubo(float fw, float fh)
{
    // Screen coordinates are Y-down (0=top, h=bottom).
    FrameUBO ubo    = screen_space_ubo(fw, fh, backend_.clip_y_down());
    ubo.pixel_scale = backend_.pixel_scale();
    backend_.set_viewport(0, 0, fw, fh);
    backend_.set_scissor(0, 0, static_cast<uint32_t>(fw), static_cast<uint32_t>(fh));
    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);
}

Rect Renderer::figure_background_region(const Figure& figure)
{
    const auto  fw    = static_cast<float>(figure.width());
    const auto  fh    = static_cast<float>(figure.height());
    const auto& style = figure.style();

    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float scale = 1.0f;   // margins are scaled with the axes text (UI scale)
    bool  any   = false;
    auto  add   = [&](const auto& axes_list)
    {
        for (const auto& ax : axes_list)
        {
            if (!ax)
                continue;
            scale          = std::max(scale, ax->text_scale());
            const Rect& vp = ax->viewport();
            if (!(vp.w > 0.0f) || !(vp.h > 0.0f))
                continue;
            if (!any)
            {
                x0  = vp.x;
                y0  = vp.y;
                x1  = vp.x + vp.w;
                y1  = vp.y + vp.h;
                any = true;
                continue;
            }
            x0 = std::min(x0, vp.x);
            y0 = std::min(y0, vp.y);
            x1 = std::max(x1, vp.x + vp.w);
            y1 = std::max(y1, vp.y + vp.h);
        }
    };
    add(figure.axes());
    add(figure.all_axes());
    if (!any)
        return Rect{0.0f, 0.0f, fw, fh};

    y0 -= style.margin_top * scale;
    if (figure.suptitle_rect().h > 0.0f)
        y0 = std::min(y0, figure.suptitle_rect().y);
    x0 = std::clamp(x0 - style.margin_left * scale, 0.0f, fw);
    y0 = std::clamp(y0, 0.0f, fh);
    x1 = std::clamp(x1 + style.margin_right * scale, 0.0f, fw);
    y1 = std::clamp(y1 + style.margin_bottom * scale, 0.0f, fh);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

void Renderer::render_figure_background(Figure& figure)
{
    const auto& style = figure.style();
    if (!style.background_override || !overlay_pipeline_ || transparent_background_)
        return;

    const Rect r = figure_background_region(figure);
    if (!(r.w >= 1.0f) || !(r.h >= 1.0f))
        return;

    const auto fw = static_cast<float>(figure.width());
    const auto fh = static_cast<float>(figure.height());

    const float verts[12] = {r.x,
                             r.y,
                             r.x + r.w,
                             r.y,
                             r.x + r.w,
                             r.y + r.h,
                             r.x,
                             r.y,
                             r.x + r.w,
                             r.y + r.h,
                             r.x,
                             r.y + r.h};

    // Per-figure, per-slot buffer: split-view figures share one command buffer
    // and the previous in-flight frame may still read the other slot.
    const uint32_t slot    = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;
    FigureGpuData& fig_gpu = figure_gpu_data_[&figure];
    if (!fig_gpu.background_buffer[slot])
        fig_gpu.background_buffer[slot] =
            backend_.create_buffer(BufferUsage::Vertex, sizeof(verts));
    backend_.upload_buffer(fig_gpu.background_buffer[slot], verts, sizeof(verts));

    FrameUBO ubo = screen_space_ubo(fw, fh, backend_.clip_y_down());
    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    backend_.bind_pipeline(overlay_pipeline_);
    // Same colour space as the theme canvas clear, so an override equal to the
    // theme colour is indistinguishable from "auto" (with or without the
    // colour vision deficiency preview, which both go through).
    const Color         bg = series_view_color(style.background);
    SeriesPushConstants pc{};
    pc.color[0] = bg.r;
    pc.color[1] = bg.g;
    pc.color[2] = bg.b;
    pc.color[3] = bg.a;
    backend_.push_constants(pc);
    backend_.bind_buffer(fig_gpu.background_buffer[slot], 0);
    backend_.draw(6);
}

// 2D grid lines of every axes in screen space, scissored to each viewport.
// Drawn after the figure background and before any series so plotted data
// (opaque bars, heatmaps, fills, plugin series) always covers the grid
// instead of the grid being stroked on top of it.
void Renderer::render_grid_2d(Figure& figure)
{
    const float    fw     = static_cast<float>(figure.width());
    const float    fh     = static_cast<float>(figure.height());
    const uint32_t slot   = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;
    const auto&    colors = plot_colors();

    bind_screen_space_ubo(fw, fh);

    // ── Draw 2D grid lines per-axes (screen-space, scissored to viewport) ──
    // Rendered in screen-space to avoid the same GPU buffer race that
    // affected the border (shared host-visible buffers overwritten while
    // previous flight frames still reference them during fast zoom).
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes2d = dynamic_cast<Axes*>(axes_ptr.get());
        if (!axes2d || !axes2d->grid_enabled())
            continue;

        const auto& vp = axes2d->viewport();

        // Skip axes entirely outside the framebuffer (scrolled off-screen) and
        // compute a scissor clamped to framebuffer bounds: Vulkan requires
        // non-negative scissor offsets (VUID-vkCmdSetScissor-x-00595).
        if (vp.x + vp.w <= 0.0f || vp.y + vp.h <= 0.0f || vp.x >= fw || vp.y >= fh)
            continue;
        const float sc_x0 = std::max(0.0f, vp.x);
        const float sc_y0 = std::max(0.0f, vp.y);
        const float sc_x1 = std::min(fw, vp.x + vp.w);
        const float sc_y1 = std::min(fh, vp.y + vp.h);
        if (!(sc_x1 > sc_x0) || !(sc_y1 > sc_y0))
            continue;

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        if (figure_vm_)
        {
            auto& avm = figure_vm_->get_or_create_axes_vm(axes2d);
            xlim      = avm.visual_xlim();
            ylim      = avm.visual_ylim();
        }
        else
        {
            xlim = axes2d->x_limits();
            ylim = axes2d->y_limits();
        }

        // Map through axis space so log axes place ticks/grid in decades.
        const ScaleType  xscale = axes2d ? axes2d->xscale() : ScaleType::Linear;
        const ScaleType  yscale = axes2d ? axes2d->yscale() : ScaleType::Linear;
        const AxisLimits ax_x   = axis_space_limits(xscale, xlim);
        const AxisLimits ax_y   = axis_space_limits(yscale, ylim);

        double x_range = ax_x.max - ax_x.min;
        double y_range = ax_y.max - ax_y.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float {
            return static_cast<float>(vp.x
                                      + (to_axis_space(xscale, dx) - ax_x.min) / x_range * vp.w);
        };
        auto data_to_px_y = [&](double dy) -> float
        {
            return static_cast<float>(
                vp.y + (1.0 - (to_axis_space(yscale, dy) - ax_y.min) / y_range) * vp.h);
        };

        auto x_ticks = axes2d->compute_x_ticks();
        auto y_ticks = axes2d->compute_y_ticks();

        size_t num_x = x_ticks.positions.size();
        size_t num_y = y_ticks.positions.size();
        if (num_x == 0 && num_y == 0)
            continue;

        grid_scratch_.clear();
        float fy_top    = vp.y;
        float fy_bottom = vp.y + vp.h;
        float fx_left   = vp.x;
        float fx_right  = vp.x + vp.w;
        for (size_t i = 0; i < num_x; ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            grid_scratch_.push_back(px);
            grid_scratch_.push_back(fy_top);
            grid_scratch_.push_back(px);
            grid_scratch_.push_back(fy_bottom);
        }
        for (size_t i = 0; i < num_y; ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            grid_scratch_.push_back(fx_left);
            grid_scratch_.push_back(py);
            grid_scratch_.push_back(fx_right);
            grid_scratch_.push_back(py);
        }

        auto grid_vert_count = static_cast<uint32_t>(grid_scratch_.size() / 2);
        if (grid_vert_count == 0)
            continue;

        size_t grid_bytes = grid_scratch_.size() * sizeof(float);
        auto&  gpu        = axes_gpu_data_[axes2d];
        if (!gpu.grid_buffer[slot] || gpu.grid_capacity[slot] < grid_bytes)
        {
            if (gpu.grid_buffer[slot])
                backend_.destroy_buffer(gpu.grid_buffer[slot]);
            gpu.grid_buffer[slot]   = backend_.create_buffer(BufferUsage::Vertex, grid_bytes * 2);
            gpu.grid_capacity[slot] = grid_bytes * 2;
        }
        backend_.upload_buffer(gpu.grid_buffer[slot], grid_scratch_.data(), grid_bytes);

        // Scissor to axes viewport so grid lines don't bleed outside
        backend_.set_scissor(static_cast<int32_t>(sc_x0),
                             static_cast<int32_t>(sc_y0),
                             static_cast<uint32_t>(sc_x1 - sc_x0),
                             static_cast<uint32_t>(sc_y1 - sc_y0));

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants grid_pc{};
        const auto&         as = axes2d->axis_style();
        if (as.grid_color.a > 0.0f)
        {
            grid_pc.color[0] = as.grid_color.r;
            grid_pc.color[1] = as.grid_color.g;
            grid_pc.color[2] = as.grid_color.b;
            grid_pc.color[3] = as.grid_color.a;
        }
        else
        {
            grid_pc.color[0] = colors.grid_major.r;
            grid_pc.color[1] = colors.grid_major.g;
            grid_pc.color[2] = colors.grid_major.b;
            grid_pc.color[3] = colors.grid_major.a;
        }
        grid_pc.line_width = as.grid_width;
        backend_.push_constants(grid_pc);

        backend_.set_line_width(std::max(1.0f, as.grid_width));
        backend_.bind_buffer(gpu.grid_buffer[slot], 0);
        backend_.draw(grid_vert_count);
        backend_.set_line_width(1.0f);

        // ── Minor grid lines (subdivisions between major ticks) ──
        // Generate 4 subdivision lines between each pair of adjacent major ticks,
        // and also extend into the margin regions before the first tick and after
        // the last tick.  Without edge extension, auto-fitted axes (which add 5%
        // padding beyond the data range) show minor grid only inside the rectangle
        // bounded by the outermost ticks, creating a visible "closed square" artifact.
        constexpr int MINOR_SUBDIVISIONS = 5;   // 5 intervals → 4 interior lines
        // Margin extension normally adds at most one major interval's worth of
        // minor lines.  Cap it so that ticks computed from limits that diverge
        // from the visual bounds (or non-finite steps) cannot hang the frame.
        constexpr int MAX_MARGIN_MINOR_LINES = 4 * MINOR_SUBDIVISIONS;
        auto valid_minor_step = [](double step) { return std::isfinite(step) && step > 0.0; };
        minor_grid_scratch_.clear();
        // Log axes: minor lines at 2..9 × 10^k (1.25..1.75 × 2^k on Log2).
        // Linear subdivision of decade
        // ticks would pile up at the low end of every decade.
        if (is_log_scale(xscale))
        {
            for (double x : log_minor_ticks(xscale,
                                            from_axis_space(xscale, ax_x.min),
                                            from_axis_space(xscale, ax_x.max)))
            {
                float px = data_to_px_x(x);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_top);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_bottom);
            }
        }
        else
        {
            // X-axis minor grid (between adjacent tick pairs)
            for (size_t i = 0; i + 1 < num_x; ++i)
            {
                double step =
                    (x_ticks.positions[i + 1] - x_ticks.positions[i]) / MINOR_SUBDIVISIONS;
                for (int s = 1; s < MINOR_SUBDIVISIONS; ++s)
                {
                    float px = data_to_px_x(x_ticks.positions[i] + step * s);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_top);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_bottom);
                }
            }
            // X-axis minor grid — extend into left margin (before first tick)
            if (num_x >= 2)
            {
                double step = (x_ticks.positions[1] - x_ticks.positions[0]) / MINOR_SUBDIVISIONS;
                for (int k = 1; valid_minor_step(step) && k <= MAX_MARGIN_MINOR_LINES; ++k)
                {
                    double x = x_ticks.positions[0] - step * k;
                    if (x < xlim.min)
                        break;
                    float px = data_to_px_x(x);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_top);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_bottom);
                }
            }
            // X-axis minor grid — extend into right margin (after last tick)
            if (num_x >= 2)
            {
                double step = (x_ticks.positions[num_x - 1] - x_ticks.positions[num_x - 2])
                              / MINOR_SUBDIVISIONS;
                for (int k = 1; valid_minor_step(step) && k <= MAX_MARGIN_MINOR_LINES; ++k)
                {
                    double x = x_ticks.positions[num_x - 1] + step * k;
                    if (x > xlim.max)
                        break;
                    float px = data_to_px_x(x);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_top);
                    minor_grid_scratch_.push_back(px);
                    minor_grid_scratch_.push_back(fy_bottom);
                }
            }
        }
        if (is_log_scale(yscale))
        {
            for (double y : log_minor_ticks(yscale,
                                            from_axis_space(yscale, ax_y.min),
                                            from_axis_space(yscale, ax_y.max)))
            {
                float py = data_to_px_y(y);
                minor_grid_scratch_.push_back(fx_left);
                minor_grid_scratch_.push_back(py);
                minor_grid_scratch_.push_back(fx_right);
                minor_grid_scratch_.push_back(py);
            }
        }
        else
        {
            // Y-axis minor grid (between adjacent tick pairs)
            for (size_t i = 0; i + 1 < num_y; ++i)
            {
                double step =
                    (y_ticks.positions[i + 1] - y_ticks.positions[i]) / MINOR_SUBDIVISIONS;
                for (int s = 1; s < MINOR_SUBDIVISIONS; ++s)
                {
                    float py = data_to_px_y(y_ticks.positions[i] + step * s);
                    minor_grid_scratch_.push_back(fx_left);
                    minor_grid_scratch_.push_back(py);
                    minor_grid_scratch_.push_back(fx_right);
                    minor_grid_scratch_.push_back(py);
                }
            }
            // Y-axis minor grid — extend into bottom margin (below first tick in data space,
            // which is the top tick position in screen space since Y is flipped)
            if (num_y >= 2)
            {
                double step = (y_ticks.positions[1] - y_ticks.positions[0]) / MINOR_SUBDIVISIONS;
                for (int k = 1; valid_minor_step(step) && k <= MAX_MARGIN_MINOR_LINES; ++k)
                {
                    double y = y_ticks.positions[0] - step * k;
                    if (y < ylim.min)
                        break;
                    float py = data_to_px_y(y);
                    minor_grid_scratch_.push_back(fx_left);
                    minor_grid_scratch_.push_back(py);
                    minor_grid_scratch_.push_back(fx_right);
                    minor_grid_scratch_.push_back(py);
                }
            }
            // Y-axis minor grid — extend into top margin (above last tick in data space)
            if (num_y >= 2)
            {
                double step = (y_ticks.positions[num_y - 1] - y_ticks.positions[num_y - 2])
                              / MINOR_SUBDIVISIONS;
                for (int k = 1; valid_minor_step(step) && k <= MAX_MARGIN_MINOR_LINES; ++k)
                {
                    double y = y_ticks.positions[num_y - 1] + step * k;
                    if (y > ylim.max)
                        break;
                    float py = data_to_px_y(y);
                    minor_grid_scratch_.push_back(fx_left);
                    minor_grid_scratch_.push_back(py);
                    minor_grid_scratch_.push_back(fx_right);
                    minor_grid_scratch_.push_back(py);
                }
            }
        }

        // Minor grid is a per-axes option (AxesBase::minor_grid).
        auto minor_vert_count = axes2d->minor_grid_enabled()
                                    ? static_cast<uint32_t>(minor_grid_scratch_.size() / 2)
                                    : 0u;
        if (minor_vert_count > 0)
        {
            size_t minor_bytes = minor_grid_scratch_.size() * sizeof(float);
            if (!gpu.minor_grid_buffer[slot] || gpu.minor_grid_capacity[slot] < minor_bytes)
            {
                if (gpu.minor_grid_buffer[slot])
                    backend_.destroy_buffer(gpu.minor_grid_buffer[slot]);
                gpu.minor_grid_buffer[slot] =
                    backend_.create_buffer(BufferUsage::Vertex, minor_bytes * 2);
                gpu.minor_grid_capacity[slot] = minor_bytes * 2;
            }
            backend_.upload_buffer(gpu.minor_grid_buffer[slot],
                                   minor_grid_scratch_.data(),
                                   minor_bytes);

            SeriesPushConstants minor_pc{};
            minor_pc.color[0]   = colors.grid_minor.r;
            minor_pc.color[1]   = colors.grid_minor.g;
            minor_pc.color[2]   = colors.grid_minor.b;
            minor_pc.color[3]   = colors.grid_minor.a;
            minor_pc.line_width = std::max(1.0f, as.grid_width * 0.5f);
            backend_.push_constants(minor_pc);

            backend_.set_line_width(minor_pc.line_width);
            backend_.bind_buffer(gpu.minor_grid_buffer[slot], 0);
            backend_.draw(minor_vert_count);
            backend_.set_line_width(1.0f);
        }
    }

    // Restore the full-figure scissor for the series that follow.
    backend_.set_scissor(0, 0, figure.width(), figure.height());
}

void Renderer::render_plot_geometry(Figure& figure)
{
    uint32_t fig_w = figure.width();
    uint32_t fig_h = figure.height();
    auto     fw    = static_cast<float>(fig_w);
    auto     fh    = static_cast<float>(fig_h);

    // Select the buffer slot for this frame so that the previous in-flight
    // frame's GPU commands still read from the other slot while we write this one.
    const uint32_t slot = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;

    const auto& colors = plot_colors();

    overlay_line_scratch_.clear();

    // ── 2D Axes: border and tick mark lines ──
    // A twinx() twin draws only its y tick marks, on the right edge (plus that
    // edge when its host is despined).
    for (Axes* axes_ptr : axes_with_twins(figure))
    {
        auto&       axes    = *axes_ptr;
        const auto& vp      = axes.viewport();
        const bool  right_y = axes.is_twin();

        // Skip axes entirely outside the framebuffer (scrolled off-screen)
        if (vp.y + vp.h < 0.0f || vp.y > fh)
            continue;
        if (right_y && axes.twin_host()->despined())
        {
            overlay_line_scratch_.push_back(vp.x + vp.w);
            overlay_line_scratch_.push_back(vp.y);
            overlay_line_scratch_.push_back(vp.x + vp.w);
            overlay_line_scratch_.push_back(vp.y + vp.h);
        }

        // ── Border: 4 lines at viewport edges (screen-space) ──
        // Rendered in screen space to avoid a GPU buffer race condition:
        // the previous data-space border vertex buffer was shared across
        // in-flight frames, causing misalignment during fast zoom.
        if (axes.border_enabled())
        {
            float bx0 = vp.x;
            float by0 = vp.y;
            float bx1 = vp.x + vp.w;
            float by1 = vp.y + vp.h;
            // Bottom edge
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by1);
            overlay_line_scratch_.push_back(bx1);
            overlay_line_scratch_.push_back(by1);
            // Top edge (hidden when despined)
            if (!axes.despined())
            {
                overlay_line_scratch_.push_back(bx0);
                overlay_line_scratch_.push_back(by0);
                overlay_line_scratch_.push_back(bx1);
                overlay_line_scratch_.push_back(by0);
            }
            // Left edge
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by0);
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by1);
            // Right edge (hidden when despined)
            if (!axes.despined())
            {
                overlay_line_scratch_.push_back(bx1);
                overlay_line_scratch_.push_back(by0);
                overlay_line_scratch_.push_back(bx1);
                overlay_line_scratch_.push_back(by1);
            }
        }

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        {
            auto* axes2d_ptr = dynamic_cast<Axes*>(&axes);
            if (figure_vm_ && axes2d_ptr)
            {
                auto& avm = figure_vm_->get_or_create_axes_vm(axes2d_ptr);
                xlim      = avm.visual_xlim();
                ylim      = avm.visual_ylim();
            }
            else
            {
                xlim = axes.x_limits();
                ylim = axes.y_limits();
            }
        }

        // Map through axis space so log axes place ticks/grid in decades.
        const auto*      scale_axes = dynamic_cast<const Axes*>(&axes);
        const ScaleType  xscale     = scale_axes ? scale_axes->xscale() : ScaleType::Linear;
        const ScaleType  yscale     = scale_axes ? scale_axes->yscale() : ScaleType::Linear;
        const AxisLimits ax_x       = axis_space_limits(xscale, xlim);
        const AxisLimits ax_y       = axis_space_limits(yscale, ylim);

        double x_range = ax_x.max - ax_x.min;
        double y_range = ax_y.max - ax_y.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float {
            return static_cast<float>(vp.x
                                      + (to_axis_space(xscale, dx) - ax_x.min) / x_range * vp.w);
        };
        auto data_to_px_y = [&](double dy) -> float
        {
            return static_cast<float>(
                vp.y + (1.0 - (to_axis_space(yscale, dy) - ax_y.min) / y_range) * vp.h);
        };

        // Tick marks grow with the UI text scale, like the tick labels.
        const float tl = axis_text_metrics(axes).tick_length;
        if (tl <= 0.0f)
            continue;

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();
        if (right_y)
            x_ticks.positions.clear();   // the host draws the x axis

        // Tick marks point out of the plot area, or into it (TickDirection::In).
        const float dir = axes.tick_direction() == TickDirection::In ? -1.0f : 1.0f;
        // Y marks stand on the left edge, or the right one for a twin.
        const float y_edge = right_y ? vp.x + vp.w : vp.x;
        const float y_out  = right_y ? 1.0f : -1.0f;
        auto        tick_marks =
            [&](const std::vector<double>& xs, const std::vector<double>& ys, float len, bool clip)
        {
            // X tick marks (at bottom edge of viewport)
            for (double position : xs)
            {
                float px = data_to_px_x(position);
                if (clip && (px < vp.x - 0.5f || px > vp.x + vp.w + 0.5f))
                    continue;
                overlay_line_scratch_.push_back(px);
                overlay_line_scratch_.push_back(vp.y + vp.h);
                overlay_line_scratch_.push_back(px);
                overlay_line_scratch_.push_back(vp.y + vp.h + len * dir);
            }
            // Y tick marks (at left edge of viewport)
            for (double position : ys)
            {
                float py = data_to_px_y(position);
                if (clip && (py < vp.y - 0.5f || py > vp.y + vp.h + 0.5f))
                    continue;
                overlay_line_scratch_.push_back(y_edge);
                overlay_line_scratch_.push_back(py);
                overlay_line_scratch_.push_back(y_edge + y_out * len * dir);
                overlay_line_scratch_.push_back(py);
            }
        };
        tick_marks(x_ticks.positions, y_ticks.positions, tl, false);
        if (axes.minor_ticks_enabled())
            tick_marks(minor_tick_values(xscale, x_ticks.positions, xlim.min, xlim.max),
                       minor_tick_values(yscale, y_ticks.positions, ylim.min, ylim.max),
                       tl * MINOR_TICK_LENGTH_RATIO,
                       true);
    }

    // NOTE: 3D axis arrows are now rendered by render_arrows() inside render_axes()
    // with depth testing, so they are properly occluded by 3D geometry.

    // ── Set up screen-space ortho projection in UBO ──
    bind_screen_space_ubo(fw, fh);

    // ── Text-annotation arrows (screen-space triangles, scissored to the plot area) ──
    {
        for (Axes* axes_ptr : axes_with_twins(figure))
            render_annotation_arrows(*axes_ptr, slot, fw, fh);
    }

    // Restore full-figure scissor for border + tick overlay
    backend_.set_scissor(0, 0, fig_w, fig_h);

    // ── Draw 2D border + tick mark lines ──
    // Use a per-figure buffer so that split-view figures each have independent
    // GPU storage.  A single shared buffer is unsafe: all figures render inside
    // ONE render pass / command buffer, so figure N's upload overwrites the
    // data that figure N-1's draw command already recorded a reference to.
    auto line_vert_count = static_cast<uint32_t>(overlay_line_scratch_.size() / 2);
    if (line_vert_count > 0)
    {
        size_t         line_bytes = overlay_line_scratch_.size() * sizeof(float);
        FigureGpuData& fig_gpu    = figure_gpu_data_[&figure];
        if (!fig_gpu.overlay_line_buffer[slot] || fig_gpu.overlay_line_capacity[slot] < line_bytes)
        {
            if (fig_gpu.overlay_line_buffer[slot])
                backend_.destroy_buffer(fig_gpu.overlay_line_buffer[slot]);
            fig_gpu.overlay_line_buffer[slot] =
                backend_.create_buffer(BufferUsage::Vertex, line_bytes * 2);
            fig_gpu.overlay_line_capacity[slot] = line_bytes * 2;
        }
        backend_.upload_buffer(fig_gpu.overlay_line_buffer[slot],
                               overlay_line_scratch_.data(),
                               line_bytes);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc{};
        set_pc_color_geom(pc.color, colors.axis_line, theme_mgr_);
        pc.line_width = 1.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(fig_gpu.overlay_line_buffer[slot], 0);
        backend_.draw(line_vert_count);
    }
}

// Append a convex polygon (screen px, `n` xy pairs) as {x, y, alpha} triangles
// for the stat_fill pipeline: an opaque fan plus a 1 px fringe fading to zero
// alpha, which anti-aliases the edges without MSAA.
static void append_aa_convex_polygon(std::vector<float>& out, const float* pts, int n)
{
    constexpr float fringe = 1.0f;
    constexpr int   max_n  = 8;
    if (n < 3 || n > max_n)
        return;

    float area2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        area2 += pts[2 * i] * pts[2 * j + 1] - pts[2 * j] * pts[2 * i + 1];
    }
    if (std::fabs(area2) < 1e-6f)
        return;
    const float orient = area2 > 0.0f ? 1.0f : -1.0f;

    // Outward edge normals, then mitred outer vertices.
    float enx[max_n];
    float eny[max_n];
    for (int i = 0; i < n; ++i)
    {
        int   j   = (i + 1) % n;
        float dx  = pts[2 * j] - pts[2 * i];
        float dy  = pts[2 * j + 1] - pts[2 * i + 1];
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f)
            len = 1.0f;
        enx[i] = orient * dy / len;
        eny[i] = -orient * dx / len;
    }
    float ox[max_n];
    float oy[max_n];
    for (int i = 0; i < n; ++i)
    {
        int   p     = (i + n - 1) % n;
        float mx    = enx[p] + enx[i];
        float my    = eny[p] + eny[i];
        float denom = std::max(0.25f, 1.0f + enx[p] * enx[i] + eny[p] * eny[i]);
        ox[i]       = pts[2 * i] + mx / denom * fringe;
        oy[i]       = pts[2 * i + 1] + my / denom * fringe;
    }

    auto push = [&out](float x, float y, float a)
    {
        out.push_back(x);
        out.push_back(y);
        out.push_back(a);
    };
    for (int i = 1; i + 1 < n; ++i)
    {
        push(pts[0], pts[1], 1.0f);
        push(pts[2 * i], pts[2 * i + 1], 1.0f);
        push(pts[2 * (i + 1)], pts[2 * (i + 1) + 1], 1.0f);
    }
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        push(pts[2 * i], pts[2 * i + 1], 1.0f);
        push(pts[2 * j], pts[2 * j + 1], 1.0f);
        push(ox[j], oy[j], 0.0f);
        push(pts[2 * i], pts[2 * i + 1], 1.0f);
        push(ox[j], oy[j], 0.0f);
        push(ox[i], oy[i], 0.0f);
    }
}

void Renderer::render_annotation_arrows(Axes& axes, uint32_t slot, float fw, float fh)
{
    const auto& vp = axes.viewport();
    if (axes.annotations().empty() || vp.x + vp.w <= 0.0f || vp.y + vp.h <= 0.0f || vp.x >= fw
        || vp.y >= fh)
        return;

    AxisLimits xlim;
    AxisLimits ylim;
    if (figure_vm_)
    {
        auto& avm = figure_vm_->get_or_create_axes_vm(&axes);
        xlim      = avm.visual_xlim();
        ylim      = avm.visual_ylim();
    }
    else
    {
        xlim = axes.x_limits();
        ylim = axes.y_limits();
    }
    // Map through axis space so arrows land on log axes like the text does.
    const ScaleType  xscale  = axes.xscale();
    const ScaleType  yscale  = axes.yscale();
    const AxisLimits ax_x    = axis_space_limits(xscale, xlim);
    const AxisLimits ax_y    = axis_space_limits(yscale, ylim);
    double           x_range = ax_x.max - ax_x.min;
    double           y_range = ax_y.max - ax_y.min;
    if (x_range == 0.0)
        x_range = 1.0;
    if (y_range == 0.0)
        y_range = 1.0;
    auto data_to_px_x = [&](double dx) -> float
    { return static_cast<float>(vp.x + (to_axis_space(xscale, dx) - ax_x.min) / x_range * vp.w); };
    auto data_to_px_y = [&](double dy) -> float
    {
        return static_cast<float>(
            vp.y + (1.0 - (to_axis_space(yscale, dy) - ax_y.min) / y_range) * vp.h);
    };

    constexpr float shaft_half_w = 0.5f;   // 1 px core + 1 px anti-aliased fringe

    annotation_tri_scratch_.clear();
    annotation_arrow_draws_.clear();
    const auto& colors = plot_colors();
    auto        rgba_of =
        [&](const std::optional<Color>& c, const ui::Color& theme_c, float theme_a, float out[4])
    {
        if (c)
        {
            out[0] = c->r;
            out[1] = c->g;
            out[2] = c->b;
            out[3] = c->a;
        }
        else
        {
            set_pc_color_geom(out, theme_c, theme_mgr_);
            out[3] = theme_a;
        }
    };
    auto push_draw = [&](uint32_t first, const float color[4])
    {
        AnnotationArrowDraw d{};
        d.first = first;
        d.count = static_cast<uint32_t>(annotation_tri_scratch_.size() / 3) - first;
        std::copy(color, color + 4, d.color);
        if (d.count > 0)
            annotation_arrow_draws_.push_back(d);
    };

    for (const auto& ann : axes.annotations())
    {
        if (!ann || !ann->visible() || (!ann->has_arrow() && !ann->bbox()))
            continue;

        const float ax = data_to_px_x(ann->x());
        const float ay = data_to_px_y(ann->y());
        if (!std::isfinite(ax) || !std::isfinite(ay))
            continue;

        float w = 0.0f;
        float h = 0.0f;
        if (text_renderer_.is_initialized() && !ann->text().empty())
        {
            const auto sf  = TextRenderer::scaled_font(ann->font_size());
            auto       ext = text_renderer_.measure_text(ann->text(), sf.preset, sf.scale);
            w              = ext.width;
            h              = ext.height;
        }

        // Background box (and its border) behind the text.
        if (ann->bbox() && !ann->text().empty())
        {
            const auto& bb  = *ann->bbox();
            const auto  box = annotation_layout::padded(
                annotation_layout::text_box(ax, ay, w, h, ann->halign(), ann->valign()),
                bb.pad);
            const auto  q      = annotation_layout::rotate_box(box, ax, ay, ann->rotation());
            const float quad[] = {q.x[0], q.y[0], q.x[1], q.y[1], q.x[2], q.y[2], q.x[3], q.y[3]};
            float       fill[4];
            rgba_of(bb.fill, colors.bg_canvas, 0.9f, fill);
            auto first = static_cast<uint32_t>(annotation_tri_scratch_.size() / 3);
            append_aa_convex_polygon(annotation_tri_scratch_, quad, 4);
            push_draw(first, fill);

            if (bb.edge_width > 0.0f)
            {
                float edge[4];
                rgba_of(bb.edge, colors.axis_line, 0.5f, edge);
                first = static_cast<uint32_t>(annotation_tri_scratch_.size() / 3);
                // Each side as a thin quad centred on the box edge.
                for (int i = 0; i < 4; ++i)
                {
                    const int   j   = (i + 1) % 4;
                    const float dx  = q.x[j] - q.x[i];
                    const float dy  = q.y[j] - q.y[i];
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (!(len > 0.0f))
                        continue;
                    // Like the arrow shaft: half width plus the AA fringe added by
                    // append_aa_convex_polygon.
                    const float hw     = bb.edge_width * 0.5f;
                    const float nx     = -dy / len * hw;
                    const float ny     = dx / len * hw;
                    const float side[] = {q.x[i] + nx,
                                          q.y[i] + ny,
                                          q.x[j] + nx,
                                          q.y[j] + ny,
                                          q.x[j] - nx,
                                          q.y[j] - ny,
                                          q.x[i] - nx,
                                          q.y[i] - ny};
                    append_aa_convex_polygon(annotation_tri_scratch_, side, 4);
                }
                push_draw(first, edge);
            }
        }

        if (!ann->has_arrow())
            continue;
        const float tx = data_to_px_x(ann->arrow_x());
        const float ty = data_to_px_y(ann->arrow_y());
        if (!std::isfinite(tx) || !std::isfinite(ty))
            continue;

        const auto               box = annotation_layout::annotation_extent(*ann, ax, ay, w, h);
        annotation_layout::Arrow g;
        if (!annotation_layout::arrow_geometry(box, tx, ty, g))
            continue;

        const auto  first   = static_cast<uint32_t>(annotation_tri_scratch_.size() / 3);
        const float hw      = shaft_half_w;
        const float shaft[] = {g.sx + g.nx * hw,
                               g.sy + g.ny * hw,
                               g.ex + g.nx * hw,
                               g.ey + g.ny * hw,
                               g.ex - g.nx * hw,
                               g.ey - g.ny * hw,
                               g.sx - g.nx * hw,
                               g.sy - g.ny * hw};
        const float head[]  = {g.tx, g.ty, g.lx, g.ly, g.rx, g.ry};
        append_aa_convex_polygon(annotation_tri_scratch_, shaft, 4);
        append_aa_convex_polygon(annotation_tri_scratch_, head, 3);

        float col[4];
        rgba_of(ann->color(), colors.text_primary, colors.text_primary.a, col);
        push_draw(first, col);
    }
    if (annotation_arrow_draws_.empty())
        return;

    if (!stat_fill_pipeline_)
        return;
    const size_t bytes = annotation_tri_scratch_.size() * sizeof(float);
    auto&        gpu   = axes_gpu_data_[&axes];
    if (!gpu.annotation_buffer[slot] || gpu.annotation_capacity[slot] < bytes)
    {
        if (gpu.annotation_buffer[slot])
            backend_.destroy_buffer(gpu.annotation_buffer[slot]);
        gpu.annotation_buffer[slot]   = backend_.create_buffer(BufferUsage::Vertex, bytes * 2);
        gpu.annotation_capacity[slot] = bytes * 2;
    }
    backend_.upload_buffer(gpu.annotation_buffer[slot], annotation_tri_scratch_.data(), bytes);

    const float sc_x0 = std::max(0.0f, vp.x);
    const float sc_y0 = std::max(0.0f, vp.y);
    const float sc_x1 = std::min(fw, vp.x + vp.w);
    const float sc_y1 = std::min(fh, vp.y + vp.h);
    if (!(sc_x1 > sc_x0) || !(sc_y1 > sc_y0))
        return;
    backend_.set_scissor(static_cast<int32_t>(sc_x0),
                         static_cast<int32_t>(sc_y0),
                         static_cast<uint32_t>(sc_x1 - sc_x0),
                         static_cast<uint32_t>(sc_y1 - sc_y0));

    backend_.bind_pipeline(stat_fill_pipeline_);
    backend_.bind_buffer(gpu.annotation_buffer[slot], 0);
    for (const auto& d : annotation_arrow_draws_)
    {
        SeriesPushConstants pc{};
        std::copy(std::begin(d.color), std::end(d.color), pc.color);
        pc.line_width = 1.0f;
        backend_.push_constants(pc);
        backend_.draw(d.count, d.first);
    }
}

void Renderer::render_grid(AxesBase& axes, const Rect& /*viewport*/)
{
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        if (!axes3d->grid_enabled())
            return;

        const uint32_t slot = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;

        auto  xlim = axes3d->x_limits();
        auto  ylim = axes3d->y_limits();
        auto  zlim = axes3d->z_limits();
        auto  gp   = axes3d->grid_planes();
        auto& gpu  = axes_gpu_data_[&axes];

        // Each grid plane sits on the box face farthest from the camera (as in
        // matplotlib / MATLAB), so it always stays behind the data.
        const axes3d_box::BoxFaces faces = axes3d_box::compute_faces(*axes3d);
        // Cache key: the enabled planes plus the back-face side of each axis.
        const int plane_key = static_cast<int>(gp) | (faces.back_hi[0] ? 0x100 : 0)
                              | (faces.back_hi[1] ? 0x200 : 0) | (faces.back_hi[2] ? 0x400 : 0);

        // Check if limits/planes changed — skip regeneration if cached
        auto& gc             = gpu.grid_cache[slot];
        bool  limits_changed = !gc.valid || gc.xmin != xlim.min || gc.xmax != xlim.max
                              || gc.ymin != ylim.min || gc.ymax != ylim.max || gc.zmin != zlim.min
                              || gc.zmax != zlim.max || gpu.cached_grid_planes[slot] != plane_key;

        if (limits_changed)
        {
            // Generate 3D grid vertices at tick positions (matches tick labels)
            Axes3DRenderer::GridPlaneData grid_gen;
            vec3                          min_corner = {static_cast<float>(xlim.min),
                                                        static_cast<float>(ylim.min),
                                                        static_cast<float>(zlim.min)};
            vec3                          max_corner = {static_cast<float>(xlim.max),
                                                        static_cast<float>(ylim.max),
                                                        static_cast<float>(zlim.max)};

            auto x_ticks_d = axes3d->compute_x_ticks().positions;
            auto y_ticks_d = axes3d->compute_y_ticks().positions;
            auto z_ticks_d = axes3d->compute_z_ticks().positions;
            // 3D grid generators expect float vectors — convert from double
            auto to_float_vec = [](const std::vector<double>& src)
            {
                std::vector<float> dst(src.size());
                for (size_t i = 0; i < src.size(); ++i)
                    dst[i] = static_cast<float>(src[i]);
                return dst;
            };
            std::vector<float> x_ticks = to_float_vec(x_ticks_d);
            std::vector<float> y_ticks = to_float_vec(y_ticks_d);
            std::vector<float> z_ticks = to_float_vec(z_ticks_d);

            const double lo[3] = {xlim.min, ylim.min, zlim.min};
            const double hi[3] = {xlim.max, ylim.max, zlim.max};
            if (static_cast<int>(gp & Axes3D::GridPlane::XY))
                grid_gen.generate_xy_plane(
                    min_corner,
                    max_corner,
                    static_cast<float>(axes3d_box::back_plane(faces, 2, lo, hi)),
                    x_ticks,
                    y_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::XZ))
                grid_gen.generate_xz_plane(
                    min_corner,
                    max_corner,
                    static_cast<float>(axes3d_box::back_plane(faces, 1, lo, hi)),
                    x_ticks,
                    z_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::YZ))
                grid_gen.generate_yz_plane(
                    min_corner,
                    max_corner,
                    static_cast<float>(axes3d_box::back_plane(faces, 0, lo, hi)),
                    y_ticks,
                    z_ticks);

            if (grid_gen.vertices.empty())
                return;

            size_t float_count = grid_gen.vertices.size() * 3;
            if (grid_scratch_.size() < float_count)
                grid_scratch_.resize(float_count);
            for (size_t i = 0; i < grid_gen.vertices.size(); ++i)
            {
                grid_scratch_[i * 3]     = grid_gen.vertices[i].x;
                grid_scratch_[i * 3 + 1] = grid_gen.vertices[i].y;
                grid_scratch_[i * 3 + 2] = grid_gen.vertices[i].z;
            }

            size_t byte_size = float_count * sizeof(float);
            if (!gpu.grid_buffer[slot] || gpu.grid_capacity[slot] < byte_size)
            {
                if (gpu.grid_buffer[slot])
                    backend_.destroy_buffer(gpu.grid_buffer[slot]);
                gpu.grid_buffer[slot] = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity[slot] = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer[slot], grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count[slot]  = static_cast<uint32_t>(float_count / 3);
            gc.xmin                      = xlim.min;
            gc.xmax                      = xlim.max;
            gc.ymin                      = ylim.min;
            gc.ymax                      = ylim.max;
            gc.zmin                      = zlim.min;
            gc.zmax                      = zlim.max;
            gpu.cached_grid_planes[slot] = plane_key;
            gc.valid                     = true;
        }

        if (!gpu.grid_buffer[slot] || gpu.grid_vertex_count[slot] == 0)
            return;

        // Depth-tested without depth writes, drawn before the box and the
        // series: anything in front of the back panes covers the grid.
        backend_.bind_pipeline(grid_overlay3d_pipeline_);

        SeriesPushConstants pc{};
        const auto&         theme_colors = plot_colors();
        float               blend        = 0.3f;
        ui::Color           blended(theme_colors.grid_major.r * (1.0f - blend) + blend,
                          theme_colors.grid_major.g * (1.0f - blend) + blend,
                          theme_colors.grid_major.b * (1.0f - blend) + blend);
        set_pc_color_geom(pc.color, blended, 0.35f, theme_mgr_);
        pc.line_width    = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(gpu.grid_buffer[slot], 0);
        backend_.draw(gpu.grid_vertex_count[slot]);
    }
    // 2D grid is now rendered in screen-space by render_plot_geometry()
    // to avoid the same GPU buffer race that affected the border.
}

void Renderer::render_axis_border(AxesBase& axes,
                                  const Rect& /*viewport*/,
                                  uint32_t /*fig_width*/,
                                  uint32_t /*fig_height*/)
{
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    // Vertices are generated relative to the view center for camera-relative
    // rendering (matching the centered projection).
    auto* axes2d = dynamic_cast<Axes*>(&axes);
    if (!axes2d)
        return;   // Border only for 2D axes

    // Phase 2 (LT-5): read limits via AxesViewModel when available
    AxisLimits xlim;
    AxisLimits ylim;
    if (figure_vm_)
    {
        auto& avm = figure_vm_->get_or_create_axes_vm(axes2d);
        xlim      = avm.visual_xlim();
        ylim      = avm.visual_ylim();
    }
    else
    {
        xlim = axes2d->x_limits();
        ylim = axes2d->y_limits();
    }

    // Retrieve view center for camera-relative coordinates
    auto&  agpu = axes_gpu_data_[&axes];
    double vcx  = agpu.view_center_x;
    double vcy  = agpu.view_center_y;

    double x_range_d = xlim.max - xlim.min;
    double y_range_d = ylim.max - ylim.min;
    if (x_range_d == 0.0)
        x_range_d = 1.0;
    if (y_range_d == 0.0)
        y_range_d = 1.0;

    // Use epsilon to prevent NDC boundary clipping
    double       eps_x   = 0.002 * x_range_d;
    double       eps_y   = 0.002 * y_range_d;
    const double MIN_EPS = 1e-15;
    if (eps_x < MIN_EPS)
        eps_x = MIN_EPS;
    if (eps_y < MIN_EPS)
        eps_y = MIN_EPS;

    // View-relative border coordinates
    auto x0 = static_cast<float>((xlim.min + eps_x) - vcx);
    auto y0 = static_cast<float>((ylim.min + eps_y) - vcy);
    auto x1 = static_cast<float>((xlim.max - eps_x) - vcx);
    auto y1 = static_cast<float>((ylim.max - eps_y) - vcy);

    float border_verts[] = {
        // Bottom edge
        x0,
        y0,
        x1,
        y0,
        // Top edge
        x0,
        y1,
        x1,
        y1,
        // Left edge
        x0,
        y0,
        x0,
        y1,
        // Right edge
        x1,
        y0,
        x1,
        y1,
    };

    size_t byte_size = sizeof(border_verts);

    // Use per-axes border buffer so multi-subplot draws don't overwrite
    // each other within the same command buffer.
    auto& gpu = axes_gpu_data_[&axes];
    if (!gpu.border_buffer || gpu.border_capacity < byte_size)
    {
        if (gpu.border_buffer)
        {
            backend_.destroy_buffer(gpu.border_buffer);
        }
        gpu.border_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.border_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.border_buffer, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = plot_colors();
    set_pc_color_geom(pc.color, theme_colors.axis_line, theme_mgr_);
    pc.line_width    = 1.0f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.border_buffer, 0);
    backend_.draw(8);   // 4 lines × 2 vertices
}

}   // namespace spectra
