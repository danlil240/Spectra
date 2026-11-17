#include "figure_serializer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_image.hpp>
#include <spectra/series_stats.hpp>

#include "overlay_snapshot.hpp"
#include "series_color_source.hpp"

#include "../dialog_env_guard.hpp"
#include "../native_dialog_policy.hpp"
#include "../../../third_party/tinyfiledialogs.h"

namespace spectra
{

// ─── Binary format constants ────────────────────────────────────────────────
static constexpr uint32_t MAGIC = 0x53504346;   // "SPCF" — Spectra Figure
// v6: double axis limits on 2D axes and double data origins
// (Series::x_offset()/y_offset()) on line and scatter series.
// v7: every series chunk ends with its colour source (palette slot / format
// letter / explicit RGB), and box plots store each box's sample count.
static constexpr uint32_t VERSION = 7;

// Chunk tags
enum ChunkTag : uint16_t
{
    TAG_FIGURE_CONFIG = 0x0001,
    TAG_FIGURE_STYLE  = 0x0002,
    TAG_LEGEND_CONFIG = 0x0003,
    TAG_SUBPLOT_GRID  = 0x0004,

    TAG_AXES_2D = 0x0010,
    TAG_AXES_3D = 0x0011,

    TAG_SERIES_LINE    = 0x0020,
    TAG_SERIES_SCATTER = 0x0021,
    TAG_SERIES_LINE3D  = 0x0022,
    TAG_SERIES_SCAT3D  = 0x0023,
    TAG_SERIES_SURFACE = 0x0024,
    TAG_SERIES_MESH    = 0x0025,
    TAG_SERIES_BOXPLOT = 0x0026,
    TAG_SERIES_VIOLIN  = 0x0027,
    TAG_SERIES_HIST    = 0x0028,
    TAG_SERIES_BAR     = 0x0029,
    TAG_SERIES_BAND    = 0x002A,

    TAG_ANNOTATION     = 0x0040,
    TAG_MARKER         = 0x0041,
    TAG_OVERLAY_CONFIG = 0x0042,
    // Axes::text()/annotate() annotation of a 2D axes.  Added without a
    // version bump: older readers skip unknown chunks.
    TAG_TEXT_ANNOTATION = 0x0043,
    // Data marker with double (absolute) x/y.  Written instead of the f32
    // TAG_MARKER, also without a version bump (older readers skip it).
    TAG_MARKER_F64 = 0x0044,
    // Axis scales + date-time mode of a 2D axes, and the category names of the
    // bar series in the preceding TAG_SERIES_BAR chunk.  Also version-free.
    TAG_BAR_CATEGORIES = 0x0045,
    TAG_AXES_FORMAT    = 0x0046,
    // AxisStyle text colours and font sizes of the preceding TAG_AXES_2D /
    // TAG_AXES_3D chunk.  Version-free; its absence marks a file written
    // before the renderer honoured these fields (they are reset to the theme
    // defaults so the figure keeps the look it was saved with).
    TAG_AXES_TEXT_STYLE = 0x0048,
    // Fill alpha / edge width / edge colour (StatFillStyle) of the bar,
    // histogram, box or violin series of the preceding series chunk; written
    // only when it differs from the default.  Version-free.
    TAG_STAT_STYLE = 0x0047,
    // Box style (BoxStyle) of a 3D axes.  Version-free: older readers skip it
    // and keep the default box.
    TAG_AXES3D_BOX = 0x0050,
    // Bar mode (spectra::BarMode) of a 2D axes, written when not the default
    // (grouped).  Version-free: older readers skip it.
    TAG_AXES_BAR_MODE = 0x0052,
    // Figure layout: constrained-layout flag, sharex / sharey modes and the
    // suptitle.  Written after the axes chunks so loading can re-link the
    // shared axes.  Version-free: older readers skip it.
    TAG_FIGURE_LAYOUT = 0x0051,
    // Equal aspect ("axis equal") flag of a 3D axes.  Version-free: older
    // readers skip it and show the axes with independent ranges.
    TAG_AXES3D_EQUAL = 0x0060,
    // Per-point colour values (+ colormap and range) and sizes of the 3D
    // scatter in the preceding TAG_SERIES_SCAT3D chunk.  Version-free.
    TAG_SCAT3D_POINT_ATTRS = 0x0061,
    // Plot-style options of an axes (minor grid / ticks, despine, tick
    // direction, default series width / marker size) and the plot style name +
    // palette of the figure.  Version-free: older readers skip them and keep
    // the defaults (which match files written before these existed).
    TAG_AXES_PLOT_STYLE   = 0x0056,
    TAG_FIGURE_PLOT_STYLE = 0x0057,
    // Grid-valued 2D series: ImageSeries (imshow / heatmap / contourf) and
    // ContourSeries (contour lines).  Version-free: older readers skip them.
    TAG_SERIES_IMAGE   = 0x0058,
    TAG_SERIES_CONTOUR = 0x0059,
    // Light mode (LightMode) and fog strength of a 3D axes; written only when
    // not the defaults.  Version-free: older readers keep a fixed light, no fog.
    TAG_AXES3D_LIGHT = 0x005A,
    // 3D style of the series in the preceding series chunk: depth shade
    // (line / scatter), edges over faces + edge colour and floor contour
    // projection + level count (surface).  Written only when not the
    // defaults.  Version-free.
    TAG_SERIES3D_STYLE = 0x005B,

    TAG_END = 0xFFFF,
};

// ─── Writer helper ──────────────────────────────────────────────────────────
class BinaryWriter
{
   public:
    explicit BinaryWriter(std::ostream& f) : f_(f) {}

    void write_u16(uint16_t v) { f_.write(reinterpret_cast<const char*>(&v), 2); }
    void write_u32(uint32_t v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_i32(int32_t v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_f32(float v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_f64(double v) { f_.write(reinterpret_cast<const char*>(&v), 8); }
    void write_u8(uint8_t v) { f_.write(reinterpret_cast<const char*>(&v), 1); }

    void write_color(const Color& c)
    {
        write_f32(c.r);
        write_f32(c.g);
        write_f32(c.b);
        write_f32(c.a);
    }

    void write_string(const std::string& s)
    {
        auto len = static_cast<uint32_t>(s.size());
        write_u32(len);
        if (len > 0)
            f_.write(s.data(), len);
    }

    void write_floats(std::span<const float> data)
    {
        auto count = static_cast<uint32_t>(data.size());
        write_u32(count);
        if (count > 0)
            f_.write(reinterpret_cast<const char*>(data.data()), count * sizeof(float));
    }

    void write_u32s(std::span<const uint32_t> data)
    {
        auto count = static_cast<uint32_t>(data.size());
        write_u32(count);
        if (count > 0)
            f_.write(reinterpret_cast<const char*>(data.data()), count * sizeof(uint32_t));
    }

    // Write chunk header. Returns position of length field for patching.
    std::streampos begin_chunk(ChunkTag tag)
    {
        write_u16(static_cast<uint16_t>(tag));
        auto pos = f_.tellp();
        write_u32(0);   // placeholder length
        return pos;
    }

    void end_chunk(std::streampos length_pos)
    {
        auto end = f_.tellp();
        auto len = static_cast<uint32_t>(end - length_pos - 4);
        f_.seekp(length_pos);
        write_u32(len);
        f_.seekp(end);
    }

    void write_series_common(const Series& s)
    {
        write_string(s.label());
        write_color(s.color());
        write_u8(static_cast<uint8_t>(s.visible()));
        write_u8(static_cast<uint8_t>(s.line_style()));
        write_u8(static_cast<uint8_t>(s.marker_style()));
        write_f32(s.marker_size());
        write_f32(s.opacity());
        write_f32(s.plot_style().line_width);
        write_u8(static_cast<uint8_t>(s.excluded_from_autoscale()));
        write_u8(static_cast<uint8_t>(s.show_in_legend()));
        write_u8(static_cast<uint8_t>(s.is_reference_line()));
    }

    // v7: ends a series chunk with the series' colour source, so palette and
    // format-letter colours keep following the theme after a reload.
    void end_series_chunk(std::streampos length_pos, const Series& s)
    {
        const SeriesColorSource src = color_source_of(s);
        write_u8(static_cast<uint8_t>(src.kind));
        write_i32(src.value);
        end_chunk(length_pos);
    }

    [[nodiscard]] bool good() const { return f_.good(); }

   private:
    std::ostream& f_;
};

// ─── Reader helper ──────────────────────────────────────────────────────────
class BinaryReader
{
   public:
    explicit BinaryReader(std::istream& f) : f_(f) {}

    uint16_t read_u16()
    {
        uint16_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 2);
        return v;
    }
    uint32_t read_u32()
    {
        uint32_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    int32_t read_i32()
    {
        int32_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    float read_f32()
    {
        float v = 0.0f;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    double read_f64()
    {
        double v = 0.0;
        f_.read(reinterpret_cast<char*>(&v), 8);
        return v;
    }
    uint8_t read_u8()
    {
        uint8_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 1);
        return v;
    }

    Color read_color() { return {read_f32(), read_f32(), read_f32(), read_f32()}; }

    std::string read_string()
    {
        uint32_t len = read_u32();
        if (len == 0)
            return {};
        std::string s(len, '\0');
        f_.read(s.data(), len);
        return s;
    }

    std::vector<float> read_floats()
    {
        uint32_t           count = read_u32();
        std::vector<float> v(count);
        if (count > 0)
            f_.read(reinterpret_cast<char*>(v.data()), count * sizeof(float));
        return v;
    }

    std::vector<uint32_t> read_u32s()
    {
        uint32_t              count = read_u32();
        std::vector<uint32_t> v(count);
        if (count > 0)
            f_.read(reinterpret_cast<char*>(v.data()), count * sizeof(uint32_t));
        return v;
    }

    void           skip(uint32_t bytes) { f_.seekg(bytes, std::ios::cur); }
    std::streampos tell() { return f_.tellg(); }
    void           seek(std::streampos pos) { f_.seekg(pos); }

    void read_series_common(Series& s)
    {
        s.label(read_string());
        s.color(read_color());
        s.visible(read_u8() != 0);
        s.line_style(static_cast<LineStyle>(read_u8()));
        s.marker_style(static_cast<MarkerStyle>(read_u8()));
        s.marker_size(read_f32());
        s.opacity(read_f32());
        float lw      = read_f32();
        auto  ps      = s.plot_style();
        ps.line_width = lw;
        s.plot_style(ps);
    }

    [[nodiscard]] bool good() const { return f_.good(); }

   private:
    std::istream& f_;
};

// ─── Colormap byte ──────────────────────────────────────────────────────────
// Low 7 bits: ColormapType; bit 7: reversed.  A user lookup table is not
// stored and loads as Viridis.

static constexpr uint8_t COLORMAP_REVERSED_BIT = 0x80;

static uint8_t encode_colormap(const Colormap& cm)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(cm.type())
                                | (cm.is_reversed() ? COLORMAP_REVERSED_BIT : 0));
}

static Colormap decode_colormap(uint8_t v)
{
    auto type = static_cast<ColormapType>(v & static_cast<uint8_t>(~COLORMAP_REVERSED_BIT));
    if (type == ColormapType::Custom)
        type = ColormapType::Viridis;
    return Colormap(type, (v & COLORMAP_REVERSED_BIT) != 0);
}

// Colormap including a user table (Colormap::from_colors): the encoded type,
// then for Custom its lookup table (reversal applied) as u32 count + RGBA8.
template <typename Writer>
static void write_colormap_full(Writer& w, const Colormap& cm)
{
    if (!cm.is_custom())
    {
        w.write_u8(encode_colormap(cm));
        w.write_u32(0);
        return;
    }
    w.write_u8(static_cast<uint8_t>(ColormapType::Custom));
    const auto lut = cm.rgba8();
    w.write_u32(static_cast<uint32_t>(Colormap::LUT_SIZE));
    for (uint8_t b : lut)
        w.write_u8(b);
}

template <typename Reader>
static Colormap read_colormap_full(Reader& r)
{
    const uint8_t  code = r.read_u8();
    const uint32_t n    = r.read_u32();
    if (n == 0 || n > Colormap::LUT_SIZE)
        return decode_colormap(code);
    std::vector<Color> colors(n);
    for (auto& c : colors)
    {
        const float rr = r.read_u8() / 255.0f;
        const float gg = r.read_u8() / 255.0f;
        const float bb = r.read_u8() / 255.0f;
        const float aa = r.read_u8() / 255.0f;
        c              = Color{rr, gg, bb, aa};
    }
    return Colormap::from_colors(colors);
}

// ─── Save implementation ────────────────────────────────────────────────────

// TAG_AXES_TEXT_STYLE payload: two colours and three f32 sizes.
static constexpr uint32_t AXES_TEXT_STYLE_BYTES = 2 * 16 + 3 * 4;

static void write_axes_text_style(BinaryWriter& w, const AxisStyle& as)
{
    auto pos = w.begin_chunk(TAG_AXES_TEXT_STYLE);
    w.write_color(as.tick_color);
    w.write_color(as.label_color);
    w.write_f32(as.label_size);
    w.write_f32(as.title_size);
    w.write_f32(as.tick_size);
    w.end_chunk(pos);
}

// Files without TAG_AXES_TEXT_STYLE were written while the renderer ignored
// these AxisStyle fields (their defaults were opaque black and a 16 px title):
// load them as the theme defaults so the figure looks as it did when saved.
// A following TAG_AXES_TEXT_STYLE chunk restores the saved values.
static void reset_legacy_axes_text_style(AxisStyle& as)
{
    const AxisStyle defaults;
    as.tick_color  = defaults.tick_color;
    as.label_color = defaults.label_color;
    as.label_size  = defaults.label_size;
    as.title_size  = defaults.title_size;
    as.tick_size   = defaults.tick_size;
}

// TAG_AXES_PLOT_STYLE flag bits.
enum : uint8_t
{
    AXES_STYLE_MINOR_GRID  = 1u << 0,
    AXES_STYLE_MINOR_TICKS = 1u << 1,
    AXES_STYLE_DESPINE     = 1u << 2,
    AXES_STYLE_TICKS_IN    = 1u << 3,
};
static constexpr uint32_t AXES_PLOT_STYLE_BYTES = 4 + 1 + 4 + 4;

static void write_axes_plot_style(BinaryWriter& w, const AxesBase& axes, int axes_index)
{
    uint8_t flags = 0;
    if (axes.minor_grid_enabled())
        flags |= AXES_STYLE_MINOR_GRID;
    if (axes.minor_ticks_enabled())
        flags |= AXES_STYLE_MINOR_TICKS;
    if (axes.despined())
        flags |= AXES_STYLE_DESPINE;
    if (axes.tick_direction() == TickDirection::In)
        flags |= AXES_STYLE_TICKS_IN;
    auto pos = w.begin_chunk(TAG_AXES_PLOT_STYLE);
    w.write_i32(axes_index);
    w.write_u8(flags);
    w.write_f32(axes.default_line_width());
    w.write_f32(axes.default_marker_size());
    w.end_chunk(pos);
}

// Fill style of a bar / histogram / box / violin series, nullptr otherwise.
static const StatFillStyle* stat_fill_style_of(const Series& s)
{
    if (auto* bs = dynamic_cast<const BarSeries*>(&s))
        return &bs->fill_style();
    if (auto* hs = dynamic_cast<const HistogramSeries*>(&s))
        return &hs->fill_style();
    if (auto* bp = dynamic_cast<const BoxPlotSeries*>(&s))
        return &bp->fill_style();
    if (auto* vs = dynamic_cast<const ViolinSeries*>(&s))
        return &vs->fill_style();
    return nullptr;
}

static bool set_stat_fill_style(Series& s, const StatFillStyle& st)
{
    if (auto* bs = dynamic_cast<BarSeries*>(&s))
        bs->fill_style(st);
    else if (auto* hs = dynamic_cast<HistogramSeries*>(&s))
        hs->fill_style(st);
    else if (auto* bp = dynamic_cast<BoxPlotSeries*>(&s))
        bp->fill_style(st);
    else if (auto* vs = dynamic_cast<ViolinSeries*>(&s))
        vs->fill_style(st);
    else
        return false;
    return true;
}

static void write_stat_style(BinaryWriter& w, const Series& s)
{
    const StatFillStyle* st = stat_fill_style_of(s);
    if (!st
        || (st->fill_alpha == STAT_DEFAULT_FILL_ALPHA && st->edge_width == STAT_DEFAULT_EDGE_WIDTH
            && !st->edge_color))
        return;
    auto pos = w.begin_chunk(TAG_STAT_STYLE);
    w.write_f32(st->fill_alpha);
    w.write_f32(st->edge_width);
    w.write_u8(static_cast<uint8_t>(st->edge_color.has_value()));
    w.write_color(st->edge_color.value_or(Color{}));
    w.end_chunk(pos);
}

static void write_axes_2d(BinaryWriter& w, const Axes& axes, int axes_index)
{
    auto pos = w.begin_chunk(TAG_AXES_2D);

    w.write_i32(axes_index);
    w.write_string(axes.title());
    w.write_string(axes.xlabel());
    w.write_string(axes.ylabel());
    w.write_u8(static_cast<uint8_t>(axes.grid_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.border_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.autoscale_mode()));

    auto xl = axes.x_limits();
    auto yl = axes.y_limits();
    w.write_f32(xl.min);
    w.write_f32(xl.max);
    w.write_f32(yl.min);
    w.write_f32(yl.max);

    // Axis style
    const auto& as = axes.axis_style();
    w.write_color(as.tick_color);
    w.write_color(as.label_color);
    w.write_color(as.grid_color);
    w.write_f32(as.tick_length);
    w.write_f32(as.label_size);
    w.write_f32(as.title_size);
    w.write_f32(as.grid_width);

    // v6: full-precision limits (the f32 copies above lose precision for
    // large values such as epoch timestamps).
    w.write_f64(xl.min);
    w.write_f64(xl.max);
    w.write_f64(yl.min);
    w.write_f64(yl.max);

    w.end_chunk(pos);
    write_axes_text_style(w, as);

    {
        auto fpos = w.begin_chunk(TAG_AXES_FORMAT);
        w.write_i32(axes_index);
        w.write_u8(static_cast<uint8_t>(axes.xscale()));
        w.write_u8(static_cast<uint8_t>(axes.yscale()));
        w.write_u8(axes.xaxis_datetime() ? 1 : 0);
        w.write_u8(axes.yaxis_datetime() ? 1 : 0);
        w.end_chunk(fpos);
    }
    write_axes_plot_style(w, axes, axes_index);

    if (axes.bar_mode() != BarMode::Grouped)
    {
        auto mpos = w.begin_chunk(TAG_AXES_BAR_MODE);
        w.write_i32(axes_index);
        w.write_u8(static_cast<uint8_t>(axes.bar_mode()));
        w.end_chunk(mpos);
    }

    // Write series for this axes
    for (const auto& sp : axes.series())
    {
        if (auto* ls = dynamic_cast<LineSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_LINE);
            w.write_i32(axes_index);
            w.write_series_common(*ls);
            w.write_f32(ls->width());
            w.write_floats(ls->x_data());
            w.write_floats(ls->y_data());
            w.write_f64(ls->x_offset());   // v6: data origins
            w.write_f64(ls->y_offset());
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* ss = dynamic_cast<ScatterSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SCATTER);
            w.write_i32(axes_index);
            w.write_series_common(*ss);
            w.write_f32(ss->size());
            w.write_floats(ss->x_data());
            w.write_floats(ss->y_data());
            w.write_u8(encode_colormap(ss->cmap()));
            w.write_f32(ss->colormap_min());
            w.write_f32(ss->colormap_max());
            w.write_u8(static_cast<uint8_t>(ss->colormap_range_set()));
            w.write_floats(ss->color_values_data());
            w.write_f64(ss->x_offset());   // v6: data origins
            w.write_f64(ss->y_offset());
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* img = dynamic_cast<ImageSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_IMAGE);
            w.write_i32(axes_index);
            w.write_series_common(*img);
            w.write_u8(static_cast<uint8_t>(img->channels()));
            w.write_u8(static_cast<uint8_t>(img->node_aligned()));
            w.write_u8(static_cast<uint8_t>(img->interpolation()));
            w.write_u8(static_cast<uint8_t>(img->origin()));
            w.write_u32(static_cast<uint32_t>(img->cols()));
            w.write_u32(static_cast<uint32_t>(img->rows()));
            w.write_floats(img->data());
            w.write_f64(img->x0());
            w.write_f64(img->x1());
            w.write_f64(img->y0());
            w.write_f64(img->y1());
            write_colormap_full(w, img->cmap());
            w.write_u8(static_cast<uint8_t>(img->colormap_range_set()));
            w.write_f32(img->vmin());
            w.write_f32(img->vmax());
            w.write_floats(img->levels());
            w.write_u8(static_cast<uint8_t>(img->show_values()));
            w.write_i32(img->value_decimals());
            for (const auto* names : {&img->x_categories(), &img->y_categories()})
            {
                w.write_u32(static_cast<uint32_t>(names->size()));
                for (const auto& n : *names)
                    w.write_string(n);
            }
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* ct = dynamic_cast<ContourSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_CONTOUR);
            w.write_i32(axes_index);
            w.write_series_common(*ct);
            w.write_f32(ct->width());
            w.write_u32(static_cast<uint32_t>(ct->cols()));
            w.write_u32(static_cast<uint32_t>(ct->rows()));
            w.write_floats(ct->z_data());
            w.write_floats(ct->x_coords());
            w.write_floats(ct->y_coords());
            w.write_floats(ct->level_values());
            write_colormap_full(w, ct->cmap());
            w.write_f32(ct->vmin());
            w.write_f32(ct->vmax());
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* band = dynamic_cast<BandSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_BAND);
            w.write_i32(axes_index);
            w.write_series_common(*band);
            w.write_f32(band->fill_opacity());
            w.write_f32(band->edge_width());
            w.write_u8(static_cast<uint8_t>(band->show_edges()));
            w.write_floats(band->x_values());
            w.write_floats(band->lower_values());
            w.write_floats(band->upper_values());
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* bp = dynamic_cast<BoxPlotSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_BOXPLOT);
            w.write_i32(axes_index);
            w.write_series_common(*bp);
            w.write_f32(bp->box_width());
            w.write_u8(static_cast<uint8_t>(bp->show_outliers()));
            w.write_u8(static_cast<uint8_t>(bp->notched()));
            w.write_u8(static_cast<uint8_t>(bp->gradient()));
            // Write box data
            auto box_count = static_cast<uint32_t>(bp->positions().size());
            w.write_u32(box_count);
            for (uint32_t i = 0; i < box_count; ++i)
            {
                w.write_f32(bp->positions()[i]);
                const auto& st = bp->stats()[i];
                w.write_f32(st.median);
                w.write_f32(st.q1);
                w.write_f32(st.q3);
                w.write_f32(st.whisker_low);
                w.write_f32(st.whisker_high);
                w.write_floats(st.outliers);
                w.write_u32(static_cast<uint32_t>(st.count));   // v7: sample count (notches)
            }
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* vs = dynamic_cast<ViolinSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_VIOLIN);
            w.write_i32(axes_index);
            w.write_series_common(*vs);
            w.write_f32(vs->violin_width());
            w.write_i32(vs->resolution());
            w.write_u8(static_cast<uint8_t>(vs->show_box()));
            w.write_u8(static_cast<uint8_t>(vs->gradient()));
            auto vcount = static_cast<uint32_t>(vs->violins().size());
            w.write_u32(vcount);
            for (const auto& vd : vs->violins())
            {
                w.write_f32(vd.x_position);
                w.write_floats(vd.values);
            }
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* hs = dynamic_cast<HistogramSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_HIST);
            w.write_i32(axes_index);
            w.write_series_common(*hs);
            w.write_i32(hs->bins());
            w.write_u8(static_cast<uint8_t>(hs->cumulative()));
            w.write_u8(static_cast<uint8_t>(hs->density()));
            w.write_u8(static_cast<uint8_t>(hs->gradient()));
            w.write_floats(hs->raw_values());
            w.end_series_chunk(spos, *sp);
        }
        else if (auto* bs = dynamic_cast<BarSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_BAR);
            w.write_i32(axes_index);
            w.write_series_common(*bs);
            w.write_f32(bs->bar_width());
            w.write_f32(bs->baseline());
            w.write_u8(static_cast<uint8_t>(bs->orientation()));
            w.write_u8(static_cast<uint8_t>(bs->gradient()));
            w.write_floats(bs->bar_positions());
            w.write_floats(bs->bar_heights());
            w.end_series_chunk(spos, *sp);
            if (!bs->categories().empty())
            {
                auto cpos = w.begin_chunk(TAG_BAR_CATEGORIES);
                w.write_u32(static_cast<uint32_t>(bs->categories().size()));
                for (const auto& name : bs->categories())
                    w.write_string(name);
                w.end_chunk(cpos);
            }
        }
        if (sp)
            write_stat_style(w, *sp);
    }

    // Text annotations, in draw order
    for (const auto& ann : axes.annotations())
    {
        if (!ann)
            continue;
        auto apos = w.begin_chunk(TAG_TEXT_ANNOTATION);
        w.write_i32(axes_index);
        w.write_f32(ann->x());
        w.write_f32(ann->y());
        w.write_string(ann->text());
        w.write_u8(ann->color() ? 1 : 0);
        w.write_color(ann->color().value_or(Color{}));
        w.write_f32(ann->font_size());
        w.write_u8(static_cast<uint8_t>(ann->halign()));
        w.write_u8(static_cast<uint8_t>(ann->valign()));
        w.write_u8(ann->has_arrow() ? 1 : 0);
        w.write_f32(ann->arrow_x());
        w.write_f32(ann->arrow_y());
        w.write_u8(ann->visible() ? 1 : 0);
        w.end_chunk(apos);
    }
}

static void write_axes_3d(BinaryWriter& w, const Axes3D& axes, int axes_index)
{
    auto pos = w.begin_chunk(TAG_AXES_3D);

    w.write_i32(axes_index);
    w.write_string(axes.title());
    w.write_string(axes.xlabel());
    w.write_string(axes.ylabel());
    w.write_string(axes.zlabel());
    w.write_u8(static_cast<uint8_t>(axes.grid_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.border_enabled()));

    auto xl = axes.x_limits();
    auto yl = axes.y_limits();
    auto zl = axes.z_limits();
    w.write_f32(xl.min);
    w.write_f32(xl.max);
    w.write_f32(yl.min);
    w.write_f32(yl.max);
    w.write_f32(zl.min);
    w.write_f32(zl.max);

    // Grid planes
    w.write_i32(static_cast<int32_t>(axes.grid_planes()));
    w.write_u8(static_cast<uint8_t>(axes.show_bounding_box()));

    // Lighting
    auto ld = axes.light_dir();
    w.write_f32(ld.x);
    w.write_f32(ld.y);
    w.write_f32(ld.z);
    w.write_u8(static_cast<uint8_t>(axes.lighting_enabled()));

    // Axis style
    const auto& as = axes.axis_style();
    w.write_color(as.tick_color);
    w.write_color(as.label_color);
    w.write_color(as.grid_color);
    w.write_f32(as.tick_length);
    w.write_f32(as.label_size);
    w.write_f32(as.title_size);
    w.write_f32(as.grid_width);

    // Camera
    const auto& cam = axes.camera();
    w.write_f32(cam.azimuth);
    w.write_f32(cam.elevation);
    w.write_f32(cam.distance);
    w.write_f32(cam.fov);
    w.write_f32(cam.near_clip);
    w.write_f32(cam.far_clip);
    w.write_f32(cam.ortho_size);
    w.write_u8(static_cast<uint8_t>(cam.projection_mode));
    w.write_f32(cam.target.x);
    w.write_f32(cam.target.y);
    w.write_f32(cam.target.z);
    w.write_f32(cam.up.x);
    w.write_f32(cam.up.y);
    w.write_f32(cam.up.z);

    w.end_chunk(pos);
    write_axes_text_style(w, as);

    {
        auto bpos = w.begin_chunk(TAG_AXES3D_BOX);
        w.write_i32(axes_index);
        w.write_u8(static_cast<uint8_t>(axes.box_style()));
        w.end_chunk(bpos);
    }
    write_axes_plot_style(w, axes, axes_index);

    if (axes.equal_aspect())
    {
        auto epos = w.begin_chunk(TAG_AXES3D_EQUAL);
        w.write_i32(axes_index);
        w.write_u8(1);
        w.end_chunk(epos);
    }

    if (axes.light_mode() != LightMode::Fixed || axes.fog() > 0.0f)
    {
        auto lpos = w.begin_chunk(TAG_AXES3D_LIGHT);
        w.write_i32(axes_index);
        w.write_u8(static_cast<uint8_t>(axes.light_mode()));
        w.write_f32(axes.fog());
        w.end_chunk(lpos);
    }

    // TAG_SERIES3D_STYLE after a series chunk: flags bit 0 depth shade, bit 1
    // edges, bit 2 contour projection; edge colour; contour level count.
    auto write_style3d = [&w](bool depth_shade, const SurfaceSeries* surf)
    {
        const bool edges    = surf && surf->show_edges();
        const bool contours = surf && surf->contour_projection();
        auto       spos     = w.begin_chunk(TAG_SERIES3D_STYLE);
        w.write_u8(
            static_cast<uint8_t>((depth_shade ? 1 : 0) | (edges ? 2 : 0) | (contours ? 4 : 0)));
        w.write_color(surf ? surf->edge_color() : SurfaceSeries{}.edge_color());
        w.write_i32(surf ? surf->contour_levels() : SurfaceSeries{}.contour_levels());
        w.end_chunk(spos);
    };

    // Write 3D series
    for (const auto& sp : axes.series())
    {
        if (auto* ls = dynamic_cast<LineSeries3D*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_LINE3D);
            w.write_i32(axes_index);
            w.write_series_common(*ls);
            w.write_f32(ls->width());
            w.write_u8(static_cast<uint8_t>(ls->blend_mode()));
            w.write_floats(ls->x_data());
            w.write_floats(ls->y_data());
            w.write_floats(ls->z_data());
            w.end_series_chunk(spos, *sp);
            if (ls->depth_shade() != LineSeries3D{}.depth_shade())
                write_style3d(ls->depth_shade(), nullptr);
        }
        else if (auto* ss = dynamic_cast<ScatterSeries3D*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SCAT3D);
            w.write_i32(axes_index);
            w.write_series_common(*ss);
            w.write_f32(ss->size());
            w.write_u8(static_cast<uint8_t>(ss->blend_mode()));
            w.write_floats(ss->x_data());
            w.write_floats(ss->y_data());
            w.write_floats(ss->z_data());
            w.end_series_chunk(spos, *sp);
            if (!ss->color_values_data().empty() || !ss->sizes_data().empty())
            {
                auto apos = w.begin_chunk(TAG_SCAT3D_POINT_ATTRS);
                w.write_u8(encode_colormap(ss->cmap()));
                w.write_u8(static_cast<uint8_t>(ss->colormap_range_set()));
                w.write_f32(ss->colormap_min());
                w.write_f32(ss->colormap_max());
                w.write_floats(ss->color_values_data());
                w.write_floats(ss->sizes_data());
                w.end_chunk(apos);
            }
            if (ss->depth_shade() != ScatterSeries3D{}.depth_shade())
                write_style3d(ss->depth_shade(), nullptr);
        }
        else if (auto* surf = dynamic_cast<SurfaceSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SURFACE);
            w.write_i32(axes_index);
            w.write_series_common(*surf);
            w.write_u8(encode_colormap(surf->cmap()));
            w.write_f32(surf->colormap_min());
            w.write_f32(surf->colormap_max());
            w.write_f32(surf->ambient());
            w.write_f32(surf->specular());
            w.write_f32(surf->shininess());
            w.write_u8(static_cast<uint8_t>(surf->blend_mode()));
            w.write_u8(static_cast<uint8_t>(surf->double_sided()));
            w.write_u8(static_cast<uint8_t>(surf->wireframe()));
            w.write_u8(static_cast<uint8_t>(surf->colormap_alpha()));
            w.write_f32(surf->colormap_alpha_min());
            w.write_f32(surf->colormap_alpha_max());
            w.write_floats(surf->x_grid());
            w.write_floats(surf->y_grid());
            w.write_floats(surf->z_values());
            // Explicit colormap range flag (before the v7 colour-source tail,
            // which older readers locate from the chunk end, so they skip it).
            w.write_u8(static_cast<uint8_t>(surf->colormap_range_set()));
            w.end_series_chunk(spos, *sp);
            const SurfaceSeries defaults;
            auto                same_color = [](const Color& a, const Color& b)
            { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
            if (surf->show_edges() || surf->contour_projection()
                || !same_color(surf->edge_color(), defaults.edge_color())
                || surf->contour_levels() != defaults.contour_levels())
                write_style3d(false, surf);
        }
        else if (auto* ms = dynamic_cast<MeshSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_MESH);
            w.write_i32(axes_index);
            w.write_series_common(*ms);
            w.write_f32(ms->ambient());
            w.write_f32(ms->specular());
            w.write_f32(ms->shininess());
            w.write_u8(static_cast<uint8_t>(ms->blend_mode()));
            w.write_u8(static_cast<uint8_t>(ms->double_sided()));
            w.write_u8(static_cast<uint8_t>(ms->wireframe()));
            w.write_floats(ms->vertices());
            w.write_u32s(ms->indices());
            w.end_series_chunk(spos, *sp);
        }
    }
}

bool FigureSerializer::save(const std::string&     path,
                            const Figure&          figure,
                            const OverlaySnapshot* overlay)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    return save(f, figure, overlay) && f.good();
}

bool FigureSerializer::save(std::ostream& f, const Figure& figure, const OverlaySnapshot* overlay)
{
    BinaryWriter w(f);

    // Header
    w.write_u32(MAGIC);
    w.write_u32(VERSION);

    // Figure config
    {
        auto pos = w.begin_chunk(TAG_FIGURE_CONFIG);
        w.write_u32(figure.width());
        w.write_u32(figure.height());
        w.end_chunk(pos);
    }

    // Figure style
    {
        auto        pos = w.begin_chunk(TAG_FIGURE_STYLE);
        const auto& s   = figure.style();
        w.write_color(s.background);
        w.write_f32(s.margin_top);
        w.write_f32(s.margin_bottom);
        w.write_f32(s.margin_left);
        w.write_f32(s.margin_right);
        w.write_f32(s.subplot_hgap);
        w.write_f32(s.subplot_vgap);
        w.write_f32(s.min_subplot_height);
        w.write_u8(static_cast<uint8_t>(s.background_override));
        w.end_chunk(pos);
    }

    // Legend config
    {
        auto        pos = w.begin_chunk(TAG_LEGEND_CONFIG);
        const auto& lc  = figure.legend();
        w.write_u8(static_cast<uint8_t>(lc.position));
        w.write_u8(static_cast<uint8_t>(lc.visible));
        w.write_f32(lc.font_size);
        w.write_color(lc.bg_color);
        w.write_color(lc.border_color);
        w.write_f32(lc.padding);
        // Explicit-colour flags (added after v3; lets opaque black be stored).
        w.write_u8(
            static_cast<uint8_t>((lc.has_bg_color ? 1u : 0u) | (lc.has_border_color ? 2u : 0u)));
        w.end_chunk(pos);
    }

    // Subplot grid — total axes = max(axes_.size, all_axes_.size) since
    // 2D axes live in axes_ and 3D axes live in all_axes_
    uint32_t total_axes =
        static_cast<uint32_t>(std::max(figure.axes().size(), figure.all_axes().size()));
    {
        auto pos = w.begin_chunk(TAG_SUBPLOT_GRID);
        w.write_i32(figure.grid_rows());
        w.write_i32(figure.grid_cols());
        w.write_u32(total_axes);
        w.end_chunk(pos);
    }

    // Write all axes and their series.
    // 2D axes are in axes_, 3D axes are in all_axes_.
    // We iterate up to total_axes and check both containers at each index.
    int axes_idx = 0;
    for (uint32_t i = 0; i < total_axes; ++i)
    {
        // Check 3D first (all_axes_)
        if (i < figure.all_axes().size() && figure.all_axes()[i])
        {
            auto* ptr = figure.all_axes()[i].get();
            if (auto* a3d = dynamic_cast<Axes3D*>(ptr))
            {
                write_axes_3d(w, *a3d, axes_idx);
                ++axes_idx;
                continue;
            }
        }
        // Check 2D (axes_)
        if (i < figure.axes().size() && figure.axes()[i])
        {
            write_axes_2d(w, *figure.axes()[i], axes_idx);
            ++axes_idx;
            continue;
        }
    }

    // Figure layout (after the axes, see TAG_FIGURE_LAYOUT)
    {
        auto pos = w.begin_chunk(TAG_FIGURE_LAYOUT);
        w.write_u8(static_cast<uint8_t>(figure.style().constrained_layout));
        w.write_u8(static_cast<uint8_t>(figure.share_x()));
        w.write_u8(static_cast<uint8_t>(figure.share_y()));
        w.write_string(figure.suptitle());
        w.end_chunk(pos);
    }

    // Write overlay state (if provided)
    if (overlay)
    {
        // Overlay config (crosshair, tooltip)
        {
            auto opos = w.begin_chunk(TAG_OVERLAY_CONFIG);
            w.write_u8(overlay->crosshair_enabled ? 1 : 0);
            w.write_u8(overlay->tooltip_enabled ? 1 : 0);
            w.end_chunk(opos);
        }

        // Markers
        for (const auto& m : overlay->markers)
        {
            auto mpos = w.begin_chunk(TAG_MARKER_F64);
            w.write_f64(m.data_x);
            w.write_f64(m.data_y);
            w.write_string(m.series_label);
            w.write_u32(static_cast<uint32_t>(m.point_index));
            w.write_i32(static_cast<int32_t>(m.axes_index));
            w.end_chunk(mpos);
        }

        // Annotations
        for (const auto& ann : overlay->annotations)
        {
            if (ann.text.empty())
                continue;
            auto apos = w.begin_chunk(TAG_ANNOTATION);
            w.write_f32(ann.data_x);
            w.write_f32(ann.data_y);
            w.write_string(ann.text);
            w.write_color(ann.color);
            w.write_f32(ann.offset_x);
            w.write_f32(ann.offset_y);
            w.write_i32(static_cast<int32_t>(ann.axes_index));
            w.end_chunk(apos);
        }
    }

    // Plot style name and figure palette (after the axes, so loading it does
    // not restyle them; it only becomes the style of axes added later).
    if (figure.style_sheet() || !figure.style().palette.empty())
    {
        auto pos = w.begin_chunk(TAG_FIGURE_PLOT_STYLE);
        w.write_string(figure.style_name());
        w.write_string(figure.style().palette);
        w.end_chunk(pos);
    }

    // End marker
    w.write_u16(static_cast<uint16_t>(TAG_END));
    w.write_u32(0);

    return w.good();
}

// ─── Load implementation ────────────────────────────────────────────────────

bool FigureSerializer::load(const std::string& path, Figure& figure, OverlaySnapshot* overlay)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    return load(f, figure, overlay);
}

bool FigureSerializer::load(std::istream& f, Figure& figure, OverlaySnapshot* overlay)
{
    BinaryReader r(f);

    // Verify header
    uint32_t magic = r.read_u32();
    if (magic != MAGIC)
        return false;

    uint32_t version = r.read_u32();
    if (version > VERSION)
        return false;

    // Clear existing figure data — use clear_series() to trigger GPU cleanup
    for (auto& ax : figure.axes_mut())
    {
        if (ax)
            ax->clear_series();
    }
    for (auto& ax : figure.all_axes_mut())
    {
        if (ax)
            ax->clear_series();
    }
    figure.axes_mut().clear();
    figure.all_axes_mut().clear();
    figure.grid_rows_ = 1;
    figure.grid_cols_ = 1;

    int grid_rows = 1;
    int grid_cols = 1;

    // Track axes as we create them: index -> AxesBase*
    std::vector<AxesBase*> axes_ptrs;
    // Bar series of the last TAG_SERIES_BAR chunk (target of TAG_BAR_CATEGORIES).
    BarSeries* last_bar = nullptr;
    // 3D scatter of the last TAG_SERIES_SCAT3D chunk (target of TAG_SCAT3D_POINT_ATTRS).
    ScatterSeries3D* last_scatter3d = nullptr;
    // Series of the last series chunk (target of TAG_STAT_STYLE).
    Series* last_series = nullptr;

    struct PendingAutoLimits
    {
        Axes*      axes;
        AxisLimits x;
        AxisLimits y;
    };
    std::vector<PendingAutoLimits> pending_auto_limits;

    // Read chunks
    while (r.good())
    {
        uint16_t tag = r.read_u16();
        uint32_t len = r.read_u32();

        if (!r.good())
            break;

        if (tag == TAG_END)
            break;

        // Series created by this chunk and the RGB saved with it (v7: the
        // colour source at the end of the chunk is applied after the switch).
        struct LoadedSeries
        {
            Series* series = nullptr;
            Color   color;
        } loaded;
        const std::streampos chunk_start = r.tell();

        switch (static_cast<ChunkTag>(tag))
        {
            case TAG_FIGURE_CONFIG:
            {
                figure.config_.width  = r.read_u32();
                figure.config_.height = r.read_u32();
                break;
            }

            case TAG_FIGURE_STYLE:
            {
                auto& s         = figure.style();
                s.background    = r.read_color();
                s.margin_top    = r.read_f32();
                s.margin_bottom = r.read_f32();
                s.margin_left   = r.read_f32();
                s.margin_right  = r.read_f32();
                s.subplot_hgap  = r.read_f32();
                s.subplot_vgap  = r.read_f32();
                if (r.good() && len >= 32)   // backward compat: field added after v3
                    s.min_subplot_height = r.read_f32();
                // backward compat: older files have no override flag, so their
                // figures follow the theme ("auto") as they always rendered.
                s.background_override = r.good() && len >= 45 && r.read_u8() != 0;
                break;
            }

            case TAG_LEGEND_CONFIG:
            {
                auto& lc        = figure.legend();
                lc.position     = static_cast<LegendPosition>(r.read_u8());
                lc.visible      = r.read_u8() != 0;
                lc.font_size    = r.read_f32();
                lc.bg_color     = r.read_color();
                lc.border_color = r.read_color();
                lc.padding      = r.read_f32();
                // backward compat: older chunks end here (42 bytes) and have no
                // explicit-colour flags, so black still means "theme colour".
                {
                    const uint8_t flags = (r.good() && len >= 43) ? r.read_u8() : 0;
                    lc.has_bg_color     = (flags & 1u) != 0;
                    lc.has_border_color = (flags & 2u) != 0;
                }
                break;
            }

            case TAG_SUBPLOT_GRID:
            {
                grid_rows = r.read_i32();
                grid_cols = r.read_i32();
                (void)
                    r.read_u32();   // axes_count (reserved for validation; consumed for alignment)
                figure.grid_rows_ = grid_rows;
                figure.grid_cols_ = grid_cols;
                break;
            }

            case TAG_AXES_2D:
            {
                int idx = r.read_i32();
                (void)idx;

                // Create axes via subplot to maintain proper grid
                int   current = static_cast<int>(figure.axes().size());
                auto& axes    = figure.subplot(grid_rows, grid_cols, current + 1);

                axes.title(r.read_string());
                axes.xlabel(r.read_string());
                axes.ylabel(r.read_string());
                axes.grid(r.read_u8() != 0);
                axes.show_border(r.read_u8() != 0);
                auto saved_autoscale = static_cast<AutoscaleMode>(r.read_u8());

                double xmin = r.read_f32();
                double xmax = r.read_f32();
                double ymin = r.read_f32();
                double ymax = r.read_f32();

                // Axis style
                auto& as       = axes.axis_style();
                as.tick_color  = r.read_color();
                as.label_color = r.read_color();
                as.grid_color  = r.read_color();
                as.tick_length = r.read_f32();
                as.label_size  = r.read_f32();
                as.title_size  = r.read_f32();
                as.grid_width  = r.read_f32();
                reset_legacy_axes_text_style(as);

                if (version >= 6)   // full-precision limits
                {
                    xmin = r.read_f64();
                    xmax = r.read_f64();
                    ymin = r.read_f64();
                    ymax = r.read_f64();
                }

                // Restore the mode first: switching to a non-Manual mode
                // clears explicit limits, so limits must be applied after it.
                axes.autoscale_mode(saved_autoscale);
                if (saved_autoscale == AutoscaleMode::Manual)
                {
                    axes.xlim(xmin, xmax);
                    axes.ylim(ymin, ymax);
                }
                else
                {
                    // The file stores the effective limits, not whether they
                    // were explicit.  Series are not loaded yet, so decide
                    // after the loop: re-apply only limits that differ from
                    // the auto-fit (i.e. a saved zoom/pan).
                    pending_auto_limits.push_back({&axes, {xmin, xmax}, {ymin, ymax}});
                }

                axes_ptrs.push_back(&axes);
                break;
            }

            case TAG_AXES_3D:
            {
                int idx = r.read_i32();
                (void)idx;

                int   current = static_cast<int>(figure.all_axes().size());
                auto& axes    = figure.subplot3d(grid_rows, grid_cols, current + 1);

                axes.title(r.read_string());
                axes.xlabel(r.read_string());
                axes.ylabel(r.read_string());
                axes.zlabel(r.read_string());
                axes.grid(r.read_u8() != 0);
                axes.show_border(r.read_u8() != 0);

                float xmin = r.read_f32();
                float xmax = r.read_f32();
                float ymin = r.read_f32();
                float ymax = r.read_f32();
                float zmin = r.read_f32();
                float zmax = r.read_f32();
                axes.xlim(xmin, xmax);
                axes.ylim(ymin, ymax);
                axes.zlim(zmin, zmax);

                axes.set_grid_planes(r.read_i32());
                axes.show_bounding_box(r.read_u8() != 0);

                // Lighting
                vec3 ld;
                ld.x = r.read_f32();
                ld.y = r.read_f32();
                ld.z = r.read_f32();
                axes.light_dir(ld);
                axes.lighting_enabled(r.read_u8() != 0);

                // Axis style
                auto& as       = axes.axis_style();
                as.tick_color  = r.read_color();
                as.label_color = r.read_color();
                as.grid_color  = r.read_color();
                as.tick_length = r.read_f32();
                as.label_size  = r.read_f32();
                as.title_size  = r.read_f32();
                as.grid_width  = r.read_f32();
                reset_legacy_axes_text_style(as);

                // Camera
                auto& cam           = axes.camera();
                cam.azimuth         = r.read_f32();
                cam.elevation       = r.read_f32();
                cam.distance        = r.read_f32();
                cam.fov             = r.read_f32();
                cam.near_clip       = r.read_f32();
                cam.far_clip        = r.read_f32();
                cam.ortho_size      = r.read_f32();
                cam.projection_mode = static_cast<Camera::ProjectionMode>(r.read_u8());
                cam.target.x        = r.read_f32();
                cam.target.y        = r.read_f32();
                cam.target.z        = r.read_f32();
                cam.up.x            = r.read_f32();
                cam.up.y            = r.read_f32();
                cam.up.z            = r.read_f32();
                cam.update_position_from_orbit();

                axes_ptrs.push_back(&axes);
                break;
            }

            case TAG_SERIES_LINE:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                auto x_data = std::vector<float>();   // read after common
                auto y_data = std::vector<float>();

                // Use temporary to read common + specific, then create series
                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                float       width = r.read_f32();

                x_data = r.read_floats();
                y_data = r.read_floats();

                // v6: double data origins; older files hold absolute values.
                const double x_origin = (version >= 6) ? r.read_f64() : 0.0;
                const double y_origin = (version >= 6) ? r.read_f64() : 0.0;

                auto& s = axes->line(x_data, y_data);
                s.x_offset(x_origin).y_offset(y_origin);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.width(width);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_SERIES_SCATTER:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                float       sz    = r.read_f32();

                auto x_data = r.read_floats();
                auto y_data = r.read_floats();

                Colormap           cmap;
                float              cmap_min      = 0.0f;
                float              cmap_max      = 1.0f;
                bool               cmap_explicit = false;
                std::vector<float> color_values;
                if (version >= 5)
                {
                    cmap          = decode_colormap(r.read_u8());
                    cmap_min      = r.read_f32();
                    cmap_max      = r.read_f32();
                    cmap_explicit = r.read_u8() != 0;
                    color_values  = r.read_floats();
                }
                // v6: double data origins; older files hold absolute values.
                const double x_origin = (version >= 6) ? r.read_f64() : 0.0;
                const double y_origin = (version >= 6) ? r.read_f64() : 0.0;

                auto& s = axes->scatter(x_data, y_data);
                s.x_offset(x_origin).y_offset(y_origin);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.size(sz);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                if (!color_values.empty())
                    s.color_values(color_values).colormap(cmap);
                if (cmap_explicit)
                    s.colormap_range(cmap_min, cmap_max);
                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_SERIES_IMAGE:
            case TAG_SERIES_CONTOUR:
            {
                const auto chunk_end = chunk_start + static_cast<std::streamoff>(len);
                int        idx       = r.read_i32();
                auto*      axes      = (idx >= 0 && idx < static_cast<int>(axes_ptrs.size()))
                                           ? dynamic_cast<Axes*>(axes_ptrs[idx])
                                           : nullptr;
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }
                std::string lbl = r.read_string();
                Color       col = r.read_color();
                bool        vis = r.read_u8() != 0;
                (void)r.read_u8();    // line style
                (void)r.read_u8();    // marker style
                (void)r.read_f32();   // marker size
                float opac = r.read_f32();
                (void)r.read_f32();   // plot-style line width
                bool excl  = r.read_u8() != 0;
                bool inleg = r.read_u8() != 0;
                (void)r.read_u8();   // reference line

                Series* made = nullptr;
                if (static_cast<ChunkTag>(tag) == TAG_SERIES_IMAGE)
                {
                    const int                channels = r.read_u8();
                    const bool               node     = r.read_u8() != 0;
                    const auto               interp = static_cast<ImageInterpolation>(r.read_u8());
                    const auto               origin = static_cast<ImageOrigin>(r.read_u8());
                    const size_t             cols   = r.read_u32();
                    const size_t             rows   = r.read_u32();
                    auto                     data   = r.read_floats();
                    const double             x0 = r.read_f64(), x1 = r.read_f64();
                    const double             y0 = r.read_f64(), y1 = r.read_f64();
                    Colormap                 cm        = read_colormap_full(r);
                    const bool               range_set = r.read_u8() != 0;
                    const float              vmin      = r.read_f32();
                    const float              vmax      = r.read_f32();
                    auto                     levels    = r.read_floats();
                    const bool               show_vals = r.read_u8() != 0;
                    const int                decimals  = r.read_i32();
                    std::vector<std::string> cats[2];
                    for (auto& names : cats)
                    {
                        const uint32_t n = r.read_u32();
                        if (n > len)   // each name takes at least 4 bytes
                            break;
                        for (uint32_t i = 0; i < n && r.good(); ++i)
                            names.push_back(r.read_string());
                    }
                    const size_t ch = channels == 4 ? 4 : 1;
                    if (!r.good() || data.size() != cols * rows * ch)
                    {
                        r.seek(chunk_end);
                        break;
                    }
                    auto& img = axes->imshow({}, 0, 0);
                    img.node_aligned(node);
                    if (ch == 4)
                        img.set_rgba(data, cols, rows, 4);
                    else
                        img.set_data(data, cols, rows);
                    img.extent(x0, x1, y0, y1)
                        .interpolation(interp)
                        .origin(origin)
                        .colormap(cm)
                        .levels(std::move(levels))
                        .show_values(show_vals, decimals);
                    if (range_set)
                        img.colormap_range(vmin, vmax);
                    if (!cats[0].empty())
                        img.x_categories(std::move(cats[0]));
                    if (!cats[1].empty())
                        img.y_categories(std::move(cats[1]));
                    made = &img;
                }
                else
                {
                    const float  width  = r.read_f32();
                    const size_t cols   = r.read_u32();
                    const size_t rows   = r.read_u32();
                    auto         z      = r.read_floats();
                    auto         xs     = r.read_floats();
                    auto         ys     = r.read_floats();
                    auto         levels = r.read_floats();
                    Colormap     cm     = read_colormap_full(r);
                    const float  vmin   = r.read_f32();
                    const float  vmax   = r.read_f32();
                    if (!r.good() || z.size() != cols * rows || xs.size() != cols
                        || ys.size() != rows)
                    {
                        r.seek(chunk_end);
                        break;
                    }
                    auto& ct = axes->contour(z, cols, rows);
                    ct.set_coordinates(xs, ys);
                    ct.levels(std::move(levels)).colormap(cm).colormap_range(vmin, vmax);
                    ct.width(width);
                    made = &ct;
                }
                made->label(lbl);
                made->color(col);
                made->visible(vis);
                made->opacity(opac);
                made->set_excluded_from_autoscale(excl);
                made->set_show_in_legend(inleg);
                loaded = {made, col};
                break;
            }

            case TAG_SERIES_BAND:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl          = r.read_string();
                Color       col          = r.read_color();
                bool        vis          = r.read_u8() != 0;
                auto        ls_e         = static_cast<LineStyle>(r.read_u8());
                auto        ms_e         = static_cast<MarkerStyle>(r.read_u8());
                float       msz          = r.read_f32();
                float       opac         = r.read_f32();
                float       lw           = r.read_f32();
                bool        excl         = (version >= 2) && (r.read_u8() != 0);
                bool        inleg        = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref        = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                float       fill_opacity = r.read_f32();
                float       edge_width   = r.read_f32();
                bool        show_edges   = r.read_u8() != 0;
                auto        x            = r.read_floats();
                auto        lower        = r.read_floats();
                auto        upper        = r.read_floats();

                auto& band = axes->band(x, lower, upper);
                band.label(lbl).color(col).visible(vis);
                band.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                band.fill_opacity(fill_opacity).edge_width(edge_width).show_edges(show_edges);
                band.set_excluded_from_autoscale(excl);
                band.set_show_in_legend(inleg);
                band.set_reference_line(isref);
                auto ps       = band.plot_style();
                ps.line_width = lw;
                band.plot_style(ps);
                loaded = {&band, col};
                break;
            }

            case TAG_SERIES_BOXPLOT:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                float bw        = r.read_f32();
                bool  show_outl = r.read_u8() != 0;
                bool  notch     = r.read_u8() != 0;
                bool  grad      = r.read_u8() != 0;

                auto& bp = axes->box_plot();
                bp.label(lbl).color(col).visible(vis);
                bp.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                bp.box_width(bw).show_outliers(show_outl).notched(notch).gradient(grad);
                bp.set_excluded_from_autoscale(excl);
                bp.set_show_in_legend(inleg);
                bp.set_reference_line(isref);

                uint32_t box_count = r.read_u32();
                for (uint32_t i = 0; i < box_count; ++i)
                {
                    float        xpos = r.read_f32();
                    BoxPlotStats st;
                    st.median       = r.read_f32();
                    st.q1           = r.read_f32();
                    st.q3           = r.read_f32();
                    st.whisker_low  = r.read_f32();
                    st.whisker_high = r.read_f32();
                    st.outliers     = r.read_floats();
                    // v7: sample count; older files leave it unknown (no notch).
                    st.count = (version >= 7) ? r.read_u32() : 0;
                    bp.add_box(xpos, std::move(st));
                }

                auto ps       = bp.plot_style();
                ps.line_width = lw;
                bp.plot_style(ps);
                loaded = {&bp, col};
                break;
            }

            case TAG_SERIES_VIOLIN:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                float vw      = r.read_f32();
                int   res     = r.read_i32();
                bool  show_bx = r.read_u8() != 0;
                bool  grad    = r.read_u8() != 0;

                auto& vs = axes->violin();
                vs.label(lbl).color(col).visible(vis);
                vs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                vs.violin_width(vw).resolution(res).show_box(show_bx).gradient(grad);
                vs.set_excluded_from_autoscale(excl);
                vs.set_show_in_legend(inleg);
                vs.set_reference_line(isref);

                uint32_t vcount = r.read_u32();
                for (uint32_t i = 0; i < vcount; ++i)
                {
                    float xpos = r.read_f32();
                    auto  vals = r.read_floats();
                    vs.add_violin(xpos, vals);
                }

                auto ps       = vs.plot_style();
                ps.line_width = lw;
                vs.plot_style(ps);
                loaded = {&vs, col};
                break;
            }

            case TAG_SERIES_HIST:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                int  bins = r.read_i32();
                bool cum  = r.read_u8() != 0;
                bool dens = r.read_u8() != 0;
                bool grad = r.read_u8() != 0;
                auto vals = r.read_floats();

                auto& hs = axes->histogram(vals, bins);
                hs.label(lbl).color(col).visible(vis);
                hs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                hs.cumulative(cum).density(dens).gradient(grad);
                hs.set_excluded_from_autoscale(excl);
                hs.set_show_in_legend(inleg);
                hs.set_reference_line(isref);

                auto ps       = hs.plot_style();
                ps.line_width = lw;
                hs.plot_style(ps);
                loaded = {&hs, col};
                break;
            }

            case TAG_SERIES_BAR:
            {
                last_bar = nullptr;
                int idx  = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                float bw   = r.read_f32();
                float base = r.read_f32();
                auto  ori  = static_cast<BarOrientation>(r.read_u8());
                bool  grad = r.read_u8() != 0;
                auto  pos  = r.read_floats();
                auto  hts  = r.read_floats();

                auto& bs = axes->bar(pos, hts);
                bs.label(lbl).color(col).visible(vis);
                bs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                bs.bar_width(bw).baseline(base).orientation(ori).gradient(grad);
                bs.set_excluded_from_autoscale(excl);
                bs.set_show_in_legend(inleg);
                bs.set_reference_line(isref);

                auto ps2       = bs.plot_style();
                ps2.line_width = lw;
                bs.plot_style(ps2);
                loaded   = {&bs, col};
                last_bar = &bs;
                break;
            }

            case TAG_SERIES_LINE3D:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                float       wid   = r.read_f32();
                auto        bm    = static_cast<BlendMode>(r.read_u8());

                auto x = r.read_floats();
                auto y = r.read_floats();
                auto z = r.read_floats();

                auto& s = axes->line3d(x, y, z);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.width(wid).blend_mode(bm);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_SERIES_SCAT3D:
            {
                last_scatter3d = nullptr;
                int idx        = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                float       sz    = r.read_f32();
                auto        bm    = static_cast<BlendMode>(r.read_u8());

                auto x = r.read_floats();
                auto y = r.read_floats();
                auto z = r.read_floats();

                auto& s        = axes->scatter3d(x, y, z);
                last_scatter3d = &s;
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.size(sz).blend_mode(bm);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_SERIES_SURFACE:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                auto  cmap     = decode_colormap(r.read_u8());
                float cmap_min = r.read_f32();
                float cmap_max = r.read_f32();
                float amb      = r.read_f32();
                float spec     = r.read_f32();
                float shin     = r.read_f32();
                auto  bm       = static_cast<BlendMode>(r.read_u8());
                bool  ds       = r.read_u8() != 0;
                bool  wf       = r.read_u8() != 0;
                bool  ca       = r.read_u8() != 0;
                float ca_min   = r.read_f32();
                float ca_max   = r.read_f32();

                auto xg = r.read_floats();
                auto yg = r.read_floats();
                auto zv = r.read_floats();

                // Explicit-range flag, when the chunk holds one (it precedes the
                // 5-byte v7 colour-source tail).  Older files always stored a
                // range, but the renderer ignored it: treat only a non-default
                // one as explicit.
                bool cmap_explicit = !(cmap_min == 0.0f && cmap_max == 1.0f);
                if (version >= 7 && r.good()
                    && static_cast<std::streamoff>(r.tell()) + 6
                           <= static_cast<std::streamoff>(chunk_start)
                                  + static_cast<std::streamoff>(len))
                    cmap_explicit = r.read_u8() != 0;

                auto& s = axes->surface(xg, yg, zv);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.colormap(cmap);
                if (cmap_explicit)
                    s.colormap_range(cmap_min, cmap_max);
                s.ambient(amb).specular(spec).shininess(shin);
                s.blend_mode(bm).double_sided(ds).wireframe(wf);
                s.colormap_alpha(ca).colormap_alpha_range(ca_min, ca_max);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_SERIES_MESH:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);

                float amb  = r.read_f32();
                float spec = r.read_f32();
                float shin = r.read_f32();
                auto  bm   = static_cast<BlendMode>(r.read_u8());
                bool  ds   = r.read_u8() != 0;
                bool  wf   = r.read_u8() != 0;

                auto verts = r.read_floats();
                auto inds  = r.read_u32s();

                auto& s = axes->mesh(verts, inds);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.ambient(amb).specular(spec).shininess(shin);
                s.blend_mode(bm).double_sided(ds).wireframe(wf);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                loaded = {&s, col};
                break;
            }

            case TAG_ANNOTATION:
            {
                if (overlay)
                {
                    OverlaySnapshot::AnnotationEntry ae;
                    ae.data_x     = r.read_f32();
                    ae.data_y     = r.read_f32();
                    ae.text       = r.read_string();
                    ae.color      = r.read_color();
                    ae.offset_x   = r.read_f32();
                    ae.offset_y   = r.read_f32();
                    int32_t ai    = r.read_i32();
                    ae.axes_index = (ai >= 0) ? static_cast<size_t>(ai) : 0;
                    overlay->annotations.push_back(std::move(ae));
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            case TAG_MARKER:
            case TAG_MARKER_F64:
            {
                if (overlay)
                {
                    const bool                   f64 = tag == TAG_MARKER_F64;
                    OverlaySnapshot::MarkerEntry me;
                    me.data_x       = f64 ? r.read_f64() : r.read_f32();
                    me.data_y       = f64 ? r.read_f64() : r.read_f32();
                    me.series_label = r.read_string();
                    me.point_index  = r.read_u32();
                    int32_t ai      = r.read_i32();
                    me.axes_index   = (ai >= 0) ? static_cast<size_t>(ai) : 0;
                    overlay->markers.push_back(std::move(me));
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            case TAG_OVERLAY_CONFIG:
            {
                if (overlay)
                {
                    overlay->crosshair_enabled = r.read_u8() != 0;
                    overlay->tooltip_enabled   = r.read_u8() != 0;
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            case TAG_TEXT_ANNOTATION:
            {
                int32_t     ai        = r.read_i32();
                float       x         = r.read_f32();
                float       y         = r.read_f32();
                std::string text      = r.read_string();
                bool        has_color = r.read_u8() != 0;
                Color       color     = r.read_color();
                float       font_size = r.read_f32();
                uint8_t     halign    = r.read_u8();
                uint8_t     valign    = r.read_u8();
                bool        has_arrow = r.read_u8() != 0;
                float       arrow_x   = r.read_f32();
                float       arrow_y   = r.read_f32();
                bool        visible   = r.read_u8() != 0;
                Axes*       axes      = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                            ? dynamic_cast<Axes*>(axes_ptrs[static_cast<size_t>(ai)])
                                            : nullptr;
                if (!axes || !r.good())
                    break;
                auto& ann = axes->text(x, y, text);
                ann.font_size(font_size)
                    .halign(static_cast<TextAnnotation::HAlign>(halign <= 2 ? halign : 0))
                    .valign(static_cast<TextAnnotation::VAlign>(valign <= 2 ? valign : 2))
                    .visible(visible);
                if (has_color)
                    ann.color(color);
                if (has_arrow)
                    ann.arrow_to(arrow_x, arrow_y);
                break;
            }

            case TAG_AXES_TEXT_STYLE:
            {
                if (len < AXES_TEXT_STYLE_BYTES)
                {
                    r.skip(len);
                    break;
                }
                AxisStyle loaded_style;
                loaded_style.tick_color  = r.read_color();
                loaded_style.label_color = r.read_color();
                loaded_style.label_size  = r.read_f32();
                loaded_style.title_size  = r.read_f32();
                loaded_style.tick_size   = r.read_f32();
                if (len > AXES_TEXT_STYLE_BYTES)
                    r.skip(len - AXES_TEXT_STYLE_BYTES);   // fields added later
                if (!r.good() || axes_ptrs.empty() || !axes_ptrs.back())
                    break;
                auto& as       = axes_ptrs.back()->axis_style();
                as.tick_color  = loaded_style.tick_color;
                as.label_color = loaded_style.label_color;
                as.label_size  = loaded_style.label_size;
                as.title_size  = loaded_style.title_size;
                as.tick_size   = loaded_style.tick_size;
                break;
            }

            case TAG_AXES3D_BOX:
            {
                const int32_t ai    = r.read_i32();
                const uint8_t style = r.read_u8();
                Axes3D*       axes  = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                          ? dynamic_cast<Axes3D*>(axes_ptrs[static_cast<size_t>(ai)])
                                          : nullptr;
                if (axes && r.good() && style <= static_cast<uint8_t>(BoxStyle::None))
                    axes->box_style(static_cast<BoxStyle>(style));
                break;
            }

            case TAG_AXES_PLOT_STYLE:
            {
                if (len < AXES_PLOT_STYLE_BYTES)
                {
                    r.skip(len);
                    break;
                }
                const int32_t ai          = r.read_i32();
                const uint8_t flags       = r.read_u8();
                const float   line_width  = r.read_f32();
                const float   marker_size = r.read_f32();
                AxesBase*     axes        = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                                ? axes_ptrs[static_cast<size_t>(ai)]
                                                : nullptr;
                r.seek(chunk_start + static_cast<std::streamoff>(len));
                if (!axes || !r.good())
                    break;
                // Only the flags and new-series defaults: the grid, tick length
                // and font sizes travel in the axes chunk, series widths in theirs.
                axes->minor_grid((flags & AXES_STYLE_MINOR_GRID) != 0);
                axes->minor_ticks((flags & AXES_STYLE_MINOR_TICKS) != 0);
                axes->despine((flags & AXES_STYLE_DESPINE) != 0);
                axes->tick_direction((flags & AXES_STYLE_TICKS_IN) ? TickDirection::In
                                                                   : TickDirection::Out);
                axes->set_series_defaults(line_width, marker_size);
                break;
            }

            case TAG_FIGURE_PLOT_STYLE:
            {
                const std::string name    = r.read_string();
                const std::string palette = r.read_string();
                r.seek(chunk_start + static_cast<std::streamoff>(len));
                if (!r.good())
                    break;
                figure.style().palette = palette;
                if (const StyleSheet* st = style::find(name))
                    figure.set_style_sheet(*st);
                break;
            }

            case TAG_FIGURE_LAYOUT:
            {
                auto share_mode = [](uint8_t v) {
                    return v <= static_cast<uint8_t>(ShareAxes::Col) ? static_cast<ShareAxes>(v)
                                                                     : ShareAxes::None;
                };
                figure.style().constrained_layout = r.read_u8() != 0;
                const ShareAxes sx                = share_mode(r.read_u8());
                const ShareAxes sy                = share_mode(r.read_u8());
                figure.suptitle(r.read_string());
                if (r.good())
                {
                    figure.share_x(sx);
                    figure.share_y(sy);
                }
                break;
            }

            case TAG_AXES3D_EQUAL:
            {
                const int32_t ai   = r.read_i32();
                const bool    on   = r.read_u8() != 0;
                Axes3D*       axes = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                         ? dynamic_cast<Axes3D*>(axes_ptrs[static_cast<size_t>(ai)])
                                         : nullptr;
                if (axes && r.good())
                    axes->equal_aspect(on);
                break;
            }

            case TAG_AXES3D_LIGHT:
            {
                if (len < 9)
                {
                    r.skip(len);
                    break;
                }
                const int32_t ai   = r.read_i32();
                const uint8_t mode = r.read_u8();
                const float   fog  = r.read_f32();
                if (len > 9)
                    r.skip(len - 9);
                Axes3D* axes = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                   ? dynamic_cast<Axes3D*>(axes_ptrs[static_cast<size_t>(ai)])
                                   : nullptr;
                if (!axes || !r.good())
                    break;
                if (mode <= static_cast<uint8_t>(LightMode::Headlight))
                    axes->light_mode(static_cast<LightMode>(mode));
                if (std::isfinite(fog))
                    axes->fog(fog);
                break;
            }

            case TAG_SERIES3D_STYLE:
            {
                constexpr uint32_t STYLE3D_BYTES = 1 + 16 + 4;
                if (!last_series || len < STYLE3D_BYTES)
                {
                    r.skip(len);
                    break;
                }
                const uint8_t flags  = r.read_u8();
                const Color   ec     = r.read_color();
                const int32_t levels = r.read_i32();
                if (len > STYLE3D_BYTES)
                    r.skip(len - STYLE3D_BYTES);
                if (!r.good())
                    break;
                const bool shade = (flags & 1) != 0;
                if (auto* l3 = dynamic_cast<LineSeries3D*>(last_series))
                    l3->depth_shade(shade);
                else if (auto* s3 = dynamic_cast<ScatterSeries3D*>(last_series))
                    s3->depth_shade(shade);
                else if (auto* surf = dynamic_cast<SurfaceSeries*>(last_series))
                {
                    surf->show_edges((flags & 2) != 0);
                    surf->edge_color(ec);
                    surf->contour_projection((flags & 4) != 0);
                    if (levels > 0 && levels <= 1000)
                        surf->contour_levels(levels);
                }
                break;
            }

            case TAG_SCAT3D_POINT_ATTRS:
            {
                const uint8_t cmap      = r.read_u8();
                const bool    range_set = r.read_u8() != 0;
                const float   cmin      = r.read_f32();
                const float   cmax      = r.read_f32();
                auto          values    = r.read_floats();
                auto          sizes     = r.read_floats();
                if (!last_scatter3d || !r.good())
                    break;
                last_scatter3d->colormap(decode_colormap(cmap));
                last_scatter3d->color_values(values);
                if (range_set)
                    last_scatter3d->colormap_range(cmin, cmax);
                last_scatter3d->sizes(sizes);
                break;
            }

            case TAG_AXES_FORMAT:
            {
                const int32_t ai   = r.read_i32();
                const uint8_t xs   = r.read_u8();
                const uint8_t ys   = r.read_u8();
                const bool    xd   = r.read_u8() != 0;
                const bool    yd   = r.read_u8() != 0;
                Axes*         axes = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                         ? dynamic_cast<Axes*>(axes_ptrs[static_cast<size_t>(ai)])
                                         : nullptr;
                if (!axes || !r.good())
                    break;
                auto to_scale = [](uint8_t v)
                { return v <= 3 ? static_cast<ScaleType>(v) : ScaleType::Linear; };
                axes->xscale(to_scale(xs));
                axes->yscale(to_scale(ys));
                axes->xaxis_datetime(xd);
                axes->yaxis_datetime(yd);
                break;
            }

            case TAG_AXES_BAR_MODE:
            {
                const int32_t ai   = r.read_i32();
                const uint8_t mode = r.read_u8();
                Axes*         axes = (ai >= 0 && static_cast<size_t>(ai) < axes_ptrs.size())
                                         ? dynamic_cast<Axes*>(axes_ptrs[static_cast<size_t>(ai)])
                                         : nullptr;
                if (axes && r.good() && mode <= static_cast<uint8_t>(BarMode::Overlay))
                    axes->bar_mode(static_cast<BarMode>(mode));
                break;
            }

            case TAG_STAT_STYLE:
            {
                constexpr uint32_t STAT_STYLE_BYTES = 4 + 4 + 1 + 16;
                if (!last_series || len < STAT_STYLE_BYTES)
                {
                    r.skip(len);
                    break;
                }
                StatFillStyle st;
                st.fill_alpha         = StatFillStyle::sanitize_alpha(r.read_f32());
                st.edge_width         = StatFillStyle::sanitize_width(r.read_f32());
                const bool  has_color = r.read_u8() != 0;
                const Color c         = r.read_color();
                if (has_color)
                    st.edge_color = c;
                if (len > STAT_STYLE_BYTES)
                    r.skip(len - STAT_STYLE_BYTES);
                if (r.good())
                    set_stat_fill_style(*last_series, st);
                break;
            }

            case TAG_BAR_CATEGORIES:
            {
                const uint32_t n = r.read_u32();
                // Each name takes at least its 4-byte length: bound the count by the chunk.
                if (!last_bar || len < 4 || n > (len - 4) / 4)
                {
                    r.skip(len - 4);
                    break;
                }
                std::vector<std::string> names;
                names.reserve(n);
                for (uint32_t i = 0; i < n && r.good(); ++i)
                    names.push_back(r.read_string());
                if (r.good())
                    last_bar->categories(std::move(names));
                break;
            }

            default:
                // Unknown chunk — skip
                r.skip(len);
                break;
        }

        if (loaded.series)
            last_series = loaded.series;

        // v7: palette / format-letter colours are restored as such, so they
        // keep following the theme.  Older files keep the saved RGB.
        constexpr uint32_t COLOR_SOURCE_BYTES = 5;   // u8 kind + i32 value
        if (loaded.series && version >= 7 && len >= COLOR_SOURCE_BYTES && r.good())
        {
            r.seek(chunk_start + static_cast<std::streamoff>(len - COLOR_SOURCE_BYTES));
            SeriesColorSource src;
            src.kind  = static_cast<SeriesColorSource::Kind>(r.read_u8());
            src.value = r.read_i32();
            if (r.good())
                apply_color_source(*loaded.series, src, loaded.color);
        }
    }

    // Treat saved limits as the auto-fit when they match it to the precision
    // they were stored with, otherwise restore them as an explicit view.
    // Before v6 they were float; v6 stores doubles, so compare relative to the
    // span (data far from zero, e.g. timestamps, has a tiny span/magnitude).
    auto matches_fit = [version](const AxisLimits& saved, const AxisLimits& fit)
    {
        const double span = fit.max - fit.min;
        double       tol  = std::abs(span) * 1e-9;
        if (version < 6)
            tol = std::max({std::abs(fit.min), std::abs(fit.max), span, 1.0}) * 1e-6;
        return std::abs(saved.min - fit.min) <= tol && std::abs(saved.max - fit.max) <= tol;
    };
    for (const auto& p : pending_auto_limits)
    {
        if (!matches_fit(p.x, p.axes->x_limits()))
            p.axes->xlim(p.x.min, p.x.max);
        if (!matches_fit(p.y, p.axes->y_limits()))
            p.axes->ylim(p.y.min, p.y.max);
    }

    return r.good() || f.eof();
}

bool FigureSerializer::has_figure_magic(std::string_view head)
{
    if (head.size() < sizeof(uint32_t))
        return false;
    uint32_t magic = 0;
    std::memcpy(&magic, head.data(), sizeof(magic));
    return magic == MAGIC;
}

// ─── Dialog wrappers ────────────────────────────────────────────────────────

bool FigureSerializer::save_with_dialog(const Figure& figure, const OverlaySnapshot* overlay)
{
    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("figure_serializer", "Save figure dialog suppressed (automation mode)");
        return false;
    }

    DialogEnvGuard env_guard;

    char const* filter_patterns[] = {"*.spectra"};
    const char* home_env          = std::getenv("HOME");
    std::string home_dir = (home_env ? std::string(home_env) + "/" : "/") + "figure.spectra";
    const char* home     = home_dir.c_str();
    char*       result   = tinyfd_saveFileDialog("Save Figure",
                                         home,
                                         1,
                                         filter_patterns,
                                         "Spectra Figure (*.spectra)");

    if (!result)
        return false;

    return save(result, figure, overlay);
}

bool FigureSerializer::load_with_dialog(Figure& figure, OverlaySnapshot* overlay)
{
    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("figure_serializer", "Load figure dialog suppressed (automation mode)");
        return false;
    }

    DialogEnvGuard env_guard;

    char const* filter_patterns[] = {"*.spectra"};
    const char* home_env          = std::getenv("HOME");
    std::string home_dir          = home_env ? std::string(home_env) + "/" : "/";
    const char* home              = home_dir.c_str();
    char*       result            = tinyfd_openFileDialog("Open Figure",
                                         home,
                                         1,
                                         filter_patterns,
                                         "Spectra Figure (*.spectra)",
                                         0);

    if (!result)
        return false;

    return load(result, figure, overlay);
}

}   // namespace spectra
