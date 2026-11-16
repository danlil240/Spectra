// Plot style presets (StyleSheet), per-axes minor grid / minor ticks / despine /
// tick direction, their SVG output, persistence, IPC transport, and the
// Export options dialog plumbing (size / DPI / scale / theme / transparency).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_image.hpp>
#include <spectra/style.hpp>
#include <string>
#include <thread>
#include <vector>

#include "agent/snapshot_figure_builder.hpp"
#include "core/axis_scale.hpp"
#include "io/vector_writer.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ipc/style_state.hpp"
#include "render/renderer.hpp"
#include "ui/app/figure_snapshot.hpp"
#include "ui/commands/style_undo.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/settings/export_settings.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;
namespace fs = std::filesystem;

namespace
{

std::vector<float> ramp(size_t n, float k = 1.0f)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(i) * k;
    return v;
}

size_t count_of(const std::string& hay, const std::string& needle)
{
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1))
        ++n;
    return n;
}

// Text of the first <g class="grid"> ... </g> group of an SVG.
std::string grid_group(const std::string& svg)
{
    const size_t a = svg.find("class=\"grid\"");
    if (a == std::string::npos)
        return {};
    const size_t b = svg.find("</g>", a);
    return svg.substr(a, b - a);
}

bool is_white(const Color& c)
{
    return c.r == 1.0f && c.g == 1.0f && c.b == 1.0f && c.a == 1.0f;
}

std::string temp_file(const std::string& name)
{
    return (fs::temp_directory_path() / ("spectra_style_test_" + name)).string();
}

}   // namespace

// ─── Built-in styles ────────────────────────────────────────────────────────

TEST(StylePresets, BuiltinsAreListedAndFindable)
{
    const auto names = style::available();
    ASSERT_GE(names.size(), 4u);
    EXPECT_EQ(names.front(), "default");
    for (const char* n : {"default", "paper", "talk", "minimal", "dark-paper"})
    {
        ASSERT_NE(style::find(n), nullptr) << n;
        EXPECT_EQ(style::find(n)->name, n);
        EXPECT_NE(std::find(names.begin(), names.end(), n), names.end()) << n;
    }
    EXPECT_EQ(style::find("no-such-style"), nullptr);
    EXPECT_EQ(style::display_name("dark-paper"), "Dark paper");
    EXPECT_EQ(style::display_name("talk"), "Talk");
}

TEST(StylePresets, DefaultStyleMatchesAxesDefaults)
{
    // The "default" style must be the built-in look, so applying it changes nothing.
    const StyleSheet& d = *style::find("default");
    Axes              ax;
    EXPECT_EQ(d.grid, ax.grid_enabled());
    EXPECT_EQ(d.minor_grid, ax.minor_grid_enabled());
    EXPECT_EQ(d.minor_ticks, ax.minor_ticks_enabled());
    EXPECT_EQ(d.despine, ax.despined());
    EXPECT_EQ(d.tick_direction, ax.tick_direction());
    EXPECT_EQ(d.tick_length, ax.axis_style().tick_length);
    EXPECT_EQ(d.label_size, ax.axis_style().label_size);
    EXPECT_EQ(d.title_size, ax.axis_style().title_size);
    EXPECT_EQ(d.line_width, ax.default_line_width());
    EXPECT_FALSE(d.background_override);
    EXPECT_TRUE(d.palette.empty());

    LineSeries plain;
    EXPECT_EQ(d.line_width, plain.width());
    EXPECT_EQ(d.marker_size, plain.marker_size());
}

TEST(StylePresets, PresetsDifferFromDefaultWhereDocumented)
{
    const StyleSheet& paper   = *style::find("paper");
    const StyleSheet& talk    = *style::find("talk");
    const StyleSheet& minimal = *style::find("minimal");
    EXPECT_TRUE(paper.background_override);
    EXPECT_TRUE(is_white(paper.background));
    EXPECT_TRUE(paper.despine);
    EXPECT_TRUE(paper.minor_ticks);
    EXPECT_FALSE(paper.grid);
    EXPECT_LT(paper.line_width, 2.0f);
    EXPECT_GT(talk.label_size, StyleSheet{}.label_size);
    EXPECT_GT(talk.title_size, StyleSheet{}.title_size);
    EXPECT_GT(talk.line_width, 2.0f);
    EXPECT_TRUE(minimal.despine);
    EXPECT_FALSE(minimal.grid);
    EXPECT_FALSE(minimal.minor_grid);
}

// ─── Figure / axes application ──────────────────────────────────────────────

TEST(StylePresets, ApplyStyleSetsFigureAndAxes)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ASSERT_TRUE(fig.apply_style("paper"));
    EXPECT_EQ(fig.style_name(), "paper");
    EXPECT_TRUE(fig.style().background_override);
    EXPECT_TRUE(is_white(fig.style().background));
    EXPECT_EQ(fig.style().palette, "colorblind");
    EXPECT_FALSE(ax.grid_enabled());
    EXPECT_FALSE(ax.minor_grid_enabled());
    EXPECT_TRUE(ax.minor_ticks_enabled());
    EXPECT_TRUE(ax.despined());
    EXPECT_EQ(ax.axis_style().label_size, style::find("paper")->label_size);
    EXPECT_EQ(fig.legend().font_size, style::find("paper")->legend_font_size);
}

TEST(StylePresets, UnknownStyleChangesNothing)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    EXPECT_FALSE(fig.apply_style("nope"));
    EXPECT_FALSE(fig.style_sheet().has_value());
    EXPECT_TRUE(ax.grid_enabled());
    EXPECT_FALSE(fig.style().background_override);
}

TEST(StylePresets, NewAxesAndSeriesFollowTheStyle)
{
    Figure fig;
    fig.apply_style("talk");
    auto&      ax   = fig.subplot(2, 1, 2);   // created after the style
    const auto x    = ramp(10);
    auto&      line = ax.plot(x, x, "-o");
    auto&      sc   = ax.scatter(x, x);
    EXPECT_FALSE(ax.minor_grid_enabled());
    EXPECT_EQ(line.width(), style::find("talk")->line_width);
    EXPECT_EQ(line.marker_size(), style::find("talk")->marker_size);
    EXPECT_EQ(sc.marker_size(), style::find("talk")->marker_size);
}

TEST(StylePresets, RestylingKeepsExplicitWidths)
{
    Figure     fig;
    auto&      ax         = fig.subplot(1, 1, 1);
    const auto x          = ramp(10);
    auto&      dflt       = ax.line(x, x);
    auto&      explicit_w = ax.line(x, x).width(5.0f);
    fig.apply_style("paper");
    EXPECT_EQ(dflt.width(), style::find("paper")->line_width);
    EXPECT_EQ(explicit_w.width(), 5.0f);
    fig.apply_style("talk");   // paper's default width follows to talk's
    EXPECT_EQ(dflt.width(), style::find("talk")->line_width);
    EXPECT_EQ(explicit_w.width(), 5.0f);
    fig.apply_style("default");
    EXPECT_EQ(dflt.width(), 2.0f);
    EXPECT_FALSE(fig.style().background_override);
    EXPECT_TRUE(fig.style().palette.empty());
    EXPECT_TRUE(ax.grid_enabled());
    EXPECT_TRUE(ax.minor_grid_enabled());
    EXPECT_FALSE(ax.despined());
}

TEST(StylePresets, CustomStyleSheet)
{
    StyleSheet s     = *style::find("minimal");
    s.name           = "mine";
    s.tick_direction = TickDirection::In;
    s.line_width     = 0.75f;
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    fig.apply_style(s);
    EXPECT_EQ(fig.style_name(), "mine");
    EXPECT_EQ(ax.tick_direction(), TickDirection::In);
    EXPECT_EQ(ax.default_line_width(), 0.75f);
}

TEST(StylePresets, UndoRestoresEverything)
{
    Figure      fig;
    auto&       ax   = fig.subplot(1, 1, 1);
    auto&       line = ax.line(ramp(5), ramp(5));
    UndoManager undo;
    undoable_apply_style(&undo, fig, *style::find("paper"));
    ASSERT_TRUE(ax.despined());
    ASSERT_TRUE(undo.undo());
    EXPECT_FALSE(ax.despined());
    EXPECT_TRUE(ax.grid_enabled());
    EXPECT_TRUE(ax.minor_grid_enabled());
    EXPECT_FALSE(fig.style().background_override);
    EXPECT_FALSE(fig.style_sheet().has_value());
    EXPECT_EQ(line.width(), 2.0f);
    EXPECT_EQ(ax.default_line_width(), 2.0f);
    ASSERT_TRUE(undo.redo());
    EXPECT_TRUE(ax.despined());
    EXPECT_EQ(fig.style_name(), "paper");
    EXPECT_EQ(line.width(), style::find("paper")->line_width);
}

TEST(StylePresets, UndoKeepsTheFigureLayout)
{
    Figure fig;
    fig.subplot(1, 1, 1);
    UndoManager undo;
    undoable_apply_style(&undo, fig, *style::find("paper"));
    // Layout changed after the style (FigureStyle also holds it).
    fig.style().constrained_layout = false;
    fig.style().margin_left        = 123.0f;
    ASSERT_TRUE(undo.undo());
    EXPECT_FALSE(fig.style().background_override);
    EXPECT_FALSE(fig.style().constrained_layout);
    EXPECT_EQ(fig.style().margin_left, 123.0f);
}

// Codex P1: undo must not touch a series deleted after the style was applied
// (Inspector trash -> deferred Axes::remove_series), nor a closed figure.
TEST(StylePresets, UndoSkipsSeriesRemovedAfterwards)
{
    Figure fig;
    auto&  ax   = fig.subplot(1, 1, 1);
    auto&  keep = ax.line(ramp(5), ramp(5));
    ax.line(ramp(5), ramp(5, 2.0f));   // deleted below
    UndoManager undo;
    undoable_apply_style(&undo, fig, *style::find("talk"));
    ASSERT_EQ(keep.width(), style::find("talk")->line_width);
    ASSERT_TRUE(ax.remove_series(1));
    auto& added = ax.line(ramp(5), ramp(5, 3.0f));   // not in the snapshot
    added.width(7.0f);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(ax.series().size(), 2u);
    EXPECT_EQ(keep.width(), 2.0f);
    EXPECT_EQ(added.width(), 7.0f);
    EXPECT_FALSE(fig.style_sheet().has_value());
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(keep.width(), style::find("talk")->line_width);
}

TEST(StylePresets, UndoIsANoOpOnceTheFigureIsClosed)
{
    auto              fig   = std::make_unique<Figure>();
    Figure*           raw   = fig.get();
    bool              alive = true;
    StyleFigureLookup lookup;
    lookup.id_of   = [&](const Figure* f) -> FigureId { return alive && f == raw ? 7 : 0; };
    lookup.resolve = [&](FigureId id) -> Figure* { return alive && id == 7 ? raw : nullptr; };
    fig->subplot(1, 1, 1).line(ramp(3), ramp(3));
    UndoManager undo;
    undoable_apply_style(&undo, *fig, *style::find("paper"), lookup);
    fig.reset();
    alive = false;
    EXPECT_TRUE(undo.undo());   // runs, touches nothing
    EXPECT_TRUE(undo.redo());
}

// Codex P2: scatter points are drawn at ScatterSeries::size().
TEST(StylePresets, ScatterSizeFollowsTheStyleAndUndo)
{
    Figure      fig;
    auto&       ax          = fig.subplot(1, 1, 1);
    auto&       dflt        = ax.scatter(ramp(4), ramp(4));
    auto&       explicit_sz = ax.scatter(ramp(4), ramp(4)).size(11.0f);
    UndoManager undo;
    undoable_apply_style(&undo, fig, *style::find("talk"));
    const float talk = style::find("talk")->marker_size;
    EXPECT_EQ(dflt.size(), talk);
    EXPECT_EQ(dflt.marker_size(), talk);
    EXPECT_EQ(explicit_sz.size(), 11.0f);
    EXPECT_EQ(ax.scatter(ramp(2), ramp(2)).size(), talk);   // new series

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(dflt.size(), 6.0f);
    EXPECT_EQ(dflt.marker_size(), 6.0f);
    EXPECT_EQ(explicit_sz.size(), 11.0f);
}

// ─── Minor ticks helper ─────────────────────────────────────────────────────

TEST(StylePresets, MinorTickValues)
{
    const std::vector<double> majors = {0.0, 1.0, 2.0};
    const auto                minor  = minor_tick_values(ScaleType::Linear, majors, -0.3, 2.3);
    // 4 between each pair, plus the margins down to -0.2 and up to 2.2.
    EXPECT_EQ(std::count_if(minor.begin(), minor.end(), [](double v) { return v > 0 && v < 2; }),
              8);
    EXPECT_NE(
        std::find_if(minor.begin(), minor.end(), [](double v) { return std::abs(v + 0.2) < 1e-9; }),
        minor.end());
    EXPECT_TRUE(
        std::all_of(minor.begin(), minor.end(), [](double v) { return v >= -0.3 && v <= 2.3; }));
    // Log axes use 2..9 x 10^k.
    const auto log_minor = minor_tick_values(ScaleType::Log10, {1.0, 10.0, 100.0}, 1.0, 100.0);
    EXPECT_EQ(log_minor.size(), 16u);
}

// ─── SVG output ─────────────────────────────────────────────────────────────

namespace
{
std::string svg_of(Figure& fig)
{
    fig.compute_layout();
    return SvgExporter::to_string(fig);
}

Figure& simple_figure(Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.line(ramp(20), ramp(20, 0.5f));
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    return fig;
}
}   // namespace

TEST(StylePresetsSvg, MinorGridIsAnOption)
{
    Figure a;
    Figure b;
    simple_figure(a);
    simple_figure(b);
    b.axes()[0]->minor_grid(false);
    const std::string ga = grid_group(svg_of(a));
    const std::string gb = grid_group(svg_of(b));
    ASSERT_FALSE(ga.empty());
    ASSERT_FALSE(gb.empty());
    // Default: two paths (minor + major); minor off: only the major one.
    EXPECT_EQ(count_of(ga, "<path"), 2u);
    EXPECT_EQ(count_of(gb, "<path"), 1u);
}

TEST(StylePresetsSvg, DespineDropsTopAndRight)
{
    Figure a;
    simple_figure(a);
    const std::string boxed = svg_of(a);
    EXPECT_NE(boxed.find("Z"), std::string::npos);   // closed border rectangle

    Figure b;
    simple_figure(b);
    b.axes()[0]->despine(true);
    const std::string open = svg_of(b);
    const Rect        vp   = b.axes()[0]->viewport();
    // Left + bottom polyline: M left,top V bottom H right — no closed rectangle.
    const std::string axis_start = "class=\"axis\"";
    const size_t      at         = open.find(axis_start);
    ASSERT_NE(at, std::string::npos);
    const std::string axis_el = open.substr(open.rfind('<', at), 400);
    EXPECT_EQ(axis_el.find('Z'), std::string::npos) << axis_el;
    EXPECT_NE(axis_el.find('V'), std::string::npos) << axis_el;
    (void)vp;
}

TEST(StylePresetsSvg, TickDirectionAndMinorTicks)
{
    Figure out;
    simple_figure(out);
    Figure in;
    simple_figure(in);
    in.axes()[0]->tick_direction(TickDirection::In);
    const std::string s_out = svg_of(out);
    const std::string s_in  = svg_of(in);
    EXPECT_NE(s_out.find("v5"), std::string::npos);
    EXPECT_NE(s_in.find("v-5"), std::string::npos);
    EXPECT_EQ(s_in.find("v5"), std::string::npos);

    Figure minor;
    simple_figure(minor);
    minor.axes()[0]->minor_ticks(true);
    const std::string s_minor = svg_of(minor);
    // Minor ticks are shorter than the major (5 px) ones.
    const float minor_len = 5.0f * MINOR_TICK_LENGTH_RATIO;
    EXPECT_GT(count_of(s_minor, "v" + vector_num(minor_len)), 4u);
    EXPECT_EQ(count_of(s_out, "v" + vector_num(minor_len)), 0u);
}

TEST(StylePresetsSvg, TransparentBackgroundDropsTheRect)
{
    Figure fig;
    simple_figure(fig);
    fig.compute_layout();
    SvgStyle st;
    EXPECT_NE(SvgExporter::to_string(fig, st).find("class=\"background\""), std::string::npos);
    st.transparent_background = true;
    EXPECT_EQ(SvgExporter::to_string(fig, st).find("class=\"background\""), std::string::npos);
    // PDF: the background fill is the first thing on the page.
    const std::string pdf_opaque = PdfExporter::to_string(fig, SvgStyle{});
    const std::string pdf_clear  = PdfExporter::to_string(fig, st);
    EXPECT_LT(pdf_clear.size(), pdf_opaque.size());
}

TEST(StylePresetsSvg, ExportOptionsCarryTransparency)
{
    Figure            fig;
    const std::string png = temp_file("t.png");
    ExportOptions     o;
    o.transparent = true;
    ASSERT_TRUE(fig.save_png(png, o));
    EXPECT_TRUE(fig.export_req_.png_transparent);
    ASSERT_TRUE(fig.save_svg(temp_file("t.svg"), o));
    EXPECT_TRUE(fig.export_req_.svg_transparent);
    ASSERT_TRUE(fig.save_pdf(temp_file("t.pdf"), ExportOptions{}));
    EXPECT_FALSE(fig.export_req_.pdf_transparent);
}

// ─── Transparent PNG pixels ─────────────────────────────────────────────────

TEST(StylePresets, UnpremultiplyRgba)
{
    // Opaque, empty, and a half-covered red edge pixel (premultiplied 128,0,0,128).
    std::vector<uint8_t> px = {10, 20, 30, 255, 0, 0, 0, 0, 128, 0, 0, 128, 51, 102, 0, 51};
    unpremultiply_rgba(px.data(), 4);
    EXPECT_EQ(px[0], 10);
    EXPECT_EQ(px[3], 255);
    EXPECT_EQ(px[7], 0);
    EXPECT_EQ(px[8], 255);
    EXPECT_EQ(px[11], 128);
    EXPECT_EQ(px[12], 255);
    EXPECT_EQ(px[13], 255);   // clamped
    EXPECT_EQ(px[15], 51);
}

// ─── Persistence (.spectra) ─────────────────────────────────────────────────

TEST(StylePresetsPersistence, SerializerRoundTrip)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.line(ramp(10), ramp(10));
    fig.apply_style("paper");
    ax.tick_direction(TickDirection::In);
    ax.minor_grid(true);   // a per-axes override on top of the style

    const std::string path = temp_file("roundtrip.spectra");
    ASSERT_TRUE(FigureSerializer::save(path, fig));
    Figure loaded;
    ASSERT_TRUE(FigureSerializer::load(path, loaded));
    fs::remove(path);

    ASSERT_FALSE(loaded.axes().empty());
    const Axes& lax = *loaded.axes()[0];
    EXPECT_TRUE(lax.despined());
    EXPECT_TRUE(lax.minor_ticks_enabled());
    EXPECT_TRUE(lax.minor_grid_enabled());
    EXPECT_FALSE(lax.grid_enabled());
    EXPECT_EQ(lax.tick_direction(), TickDirection::In);
    EXPECT_EQ(lax.default_line_width(), style::find("paper")->line_width);
    EXPECT_EQ(loaded.style_name(), "paper");
    EXPECT_EQ(loaded.style().palette, "colorblind");
    EXPECT_TRUE(loaded.style().background_override);
}

// Codex P2: the Export dialog copies the figure through FigureSerializer, so
// image / heatmap / contourf / contour series must survive it.
TEST(StylePresetsPersistence, ImageAndContourSeriesRoundTrip)
{
    Figure                   fig;
    auto&                    ax = fig.subplot(1, 1, 1);
    const std::vector<float> z{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};   // 4 x 3
    auto&                    img = ax.imshow(z, 4, 3);
    img.extent(-1.0, 3.0, 10.0, 16.0).colormap("magma_r").colormap_range(1.0f, 9.0f);
    auto& hm = ax.heatmap(z, 4, 3, {"a", "b", "c", "d"}, {"x", "y", "z"});
    hm.show_values(true, 1);
    auto&                    cf = ax.contourf(z, 4, 3, 5);
    const std::vector<Color> stops{colors::red, colors::blue};
    auto&                    ct = ax.contour(z, 4, 3, 4);
    ct.colormap(Colormap::from_colors(stops)).width(2.5f).label("iso");
    std::vector<float> rgba(2 * 2 * 4, 0.5f);
    auto&              pic = ax.imshow({}, 0, 0);
    pic.set_rgba(rgba, 2, 2, 4);

    const std::string path = temp_file("grid.spectra");
    ASSERT_TRUE(FigureSerializer::save(path, fig));
    Figure loaded;
    ASSERT_TRUE(FigureSerializer::load(path, loaded));
    fs::remove(path);
    ASSERT_FALSE(loaded.axes().empty());
    const auto& series = loaded.axes()[0]->series();
    ASSERT_EQ(series.size(), 5u);

    auto* limg = dynamic_cast<ImageSeries*>(series[0].get());
    ASSERT_NE(limg, nullptr);
    EXPECT_EQ(limg->cols(), 4u);
    EXPECT_EQ(limg->rows(), 3u);
    EXPECT_EQ(limg->x0(), -1.0);
    EXPECT_EQ(limg->y1(), 16.0);
    EXPECT_EQ(limg->cmap(), img.cmap());
    EXPECT_TRUE(limg->colormap_range_set());
    EXPECT_EQ(limg->vmax(), 9.0f);

    auto* lhm = dynamic_cast<ImageSeries*>(series[1].get());
    ASSERT_NE(lhm, nullptr);
    EXPECT_TRUE(lhm->show_values());
    EXPECT_EQ(lhm->value_decimals(), 1);
    EXPECT_EQ(lhm->x_categories(), hm.x_categories());
    EXPECT_EQ(lhm->y_categories(), hm.y_categories());

    auto* lcf = dynamic_cast<ImageSeries*>(series[2].get());
    ASSERT_NE(lcf, nullptr);
    EXPECT_EQ(lcf->levels(), cf.levels());
    EXPECT_TRUE(lcf->node_aligned());
    EXPECT_EQ(lcf->origin(), ImageOrigin::Lower);
    EXPECT_EQ(lcf->x1(), cf.x1());

    auto* lct = dynamic_cast<ContourSeries*>(series[3].get());
    ASSERT_NE(lct, nullptr);
    EXPECT_EQ(lct->label(), "iso");
    EXPECT_EQ(lct->width(), 2.5f);
    EXPECT_EQ(lct->level_values(), ct.level_values());
    EXPECT_EQ(lct->point_count(), ct.point_count());
    EXPECT_TRUE(lct->cmap().is_custom());
    EXPECT_EQ(lct->cmap().rgba8(), ct.cmap().rgba8());

    auto* lpic = dynamic_cast<ImageSeries*>(series[4].get());
    ASSERT_NE(lpic, nullptr);
    EXPECT_TRUE(lpic->is_rgba());
    EXPECT_EQ(lpic->cols(), 2u);
}

TEST(StylePresetsPersistence, DefaultFigureLoadsDefaults)
{
    Figure fig;
    fig.subplot(1, 1, 1);
    const std::string path = temp_file("default.spectra");
    ASSERT_TRUE(FigureSerializer::save(path, fig));
    Figure loaded;
    ASSERT_TRUE(FigureSerializer::load(path, loaded));
    fs::remove(path);
    ASSERT_FALSE(loaded.axes().empty());
    EXPECT_TRUE(loaded.axes()[0]->minor_grid_enabled());
    EXPECT_FALSE(loaded.axes()[0]->despined());
    EXPECT_FALSE(loaded.style_sheet().has_value());
}

TEST(StylePresetsPersistence, WorkspaceRoundTrip)
{
    Figure fig;
    fig.subplot(1, 1, 1);
    fig.apply_style("minimal");
    fig.axes()[0]->minor_ticks(true);
    fig.axes()[0]->tick_direction(TickDirection::In);
    std::vector<Figure*> figs{&fig};
    WorkspaceData        data = Workspace::capture(figs, 0, "dark", true, 300.0f, false);
    const std::string    path = temp_file("style.spectra-workspace");
    ASSERT_TRUE(Workspace::save(path, data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    fs::remove(path);
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_EQ(loaded.figures[0].plot_style, "minimal");

    Figure target;
    target.subplot(1, 1, 1);
    std::vector<Figure*> targets{&target};
    ASSERT_TRUE(Workspace::apply(loaded, targets));
    const auto& ax = *target.axes()[0];
    EXPECT_EQ(target.style_name(), "minimal");
    EXPECT_TRUE(ax.despined());
    EXPECT_FALSE(ax.grid_enabled());
    EXPECT_TRUE(ax.minor_ticks_enabled());
    EXPECT_EQ(ax.tick_direction(), TickDirection::In);
}

// The Export dialog renders through a headless App on a worker thread: its
// ThemeManager must not replace (or, on exit, clear) the UI thread's one.
TEST(ExportPreviewThreading, NestedThemeManagerIsThreadScoped)
{
    ui::ThemeManager outer;
    outer.ensure_initialized();
    ui::ThemeManager::claim_current(&outer);
    ASSERT_EQ(&ui::ThemeManager::instance(), &outer);

    bool        worker_saw_own = false;
    std::thread worker(
        [&]
        {
            ui::ThemeManager inner;
            inner.ensure_initialized();
            ui::ThemeManager::claim_current(&inner);
            worker_saw_own = &ui::ThemeManager::instance() == &inner;
            ui::ThemeManager::release_current(&inner);
        });
    worker.join();

    EXPECT_TRUE(worker_saw_own);
    EXPECT_EQ(&ui::ThemeManager::instance(), &outer);
    ui::ThemeManager::release_current(&outer);
}

// ─── IPC (multiproc / Python) ───────────────────────────────────────────────

TEST(StylePresetsIpc, SnapshotCarriesStyleName)
{
    Figure fig;
    fig.subplot(1, 1, 1).line(ramp(4), ramp(4));
    fig.apply_style("minimal");
    const ipc::SnapshotFigureState snap = figure_to_snapshot(fig, 7);
    EXPECT_EQ(snap.style, "minimal");

    ipc::StateSnapshotPayload payload;
    payload.figures.push_back(snap);
    const auto decoded = ipc::decode_state_snapshot(ipc::encode_state_snapshot(payload));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 1u);
    EXPECT_EQ(decoded->figures[0].style, "minimal");

    auto built = agent::build_figure_from_snapshot(decoded->figures[0]);
    ASSERT_TRUE(built);
    EXPECT_EQ(built->style_name(), "minimal");
    ASSERT_FALSE(built->axes().empty());
    EXPECT_TRUE(built->axes()[0]->despined());
    EXPECT_FALSE(built->axes()[0]->grid_enabled());
}

// Wire value shared with the FlatBuffers schema; 32-42 belong to other changes.
static_assert(static_cast<int>(ipc::DiffOp::Type::SET_FIGURE_STYLE) == 43);

TEST(StylePresetsIpc, StyleDiffOpRoundTrip)
{
    ipc::StateDiffPayload diff;
    diff.ops.push_back(ipc::make_figure_style_op(3, "talk"));
    const auto decoded = ipc::decode_state_diff(ipc::encode_state_diff(diff));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, ipc::DiffOp::Type::SET_FIGURE_STYLE);
    EXPECT_EQ(decoded->ops[0].str_val, "talk");
    EXPECT_EQ(decoded->ops[0].figure_id, 3u);
}

TEST(StylePresetsIpc, FigureModelRestylesAndStylesNewItems)
{
    daemon::FigureModel model;
    const uint64_t      id = model.create_figure("f", 640, 480);
    model.add_axes(id, 0, 1, 0, 1, false);
    uint32_t si = 0;
    model.add_series_with_diff(id, "a", "line", 0, si);

    ipc::DiffOp op;
    ASSERT_TRUE(model.set_figure_style(id, "paper", &op));
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_FIGURE_STYLE);
    EXPECT_FALSE(model.set_figure_style(id, "bogus", nullptr));

    model.add_axes(id, 0, 1, 0, 1, false);
    uint32_t si2 = 0;
    model.add_series_with_diff(id, "b", "line", 1, si2);

    const auto snap = model.snapshot({id});
    ASSERT_EQ(snap.figures.size(), 1u);
    const auto& f = snap.figures[0];
    EXPECT_EQ(f.style, "paper");
    ASSERT_EQ(f.axes.size(), 2u);
    EXPECT_FALSE(f.axes[0].grid_visible);
    EXPECT_FALSE(f.axes[1].grid_visible);
    ASSERT_EQ(f.series.size(), 2u);
    EXPECT_EQ(f.series[0].line_width, style::find("paper")->line_width);
    EXPECT_EQ(f.series[1].line_width, style::find("paper")->line_width);

    // The window's cache mirrors it through the diff op.
    ipc::SnapshotFigureState cache;
    cache.axes.resize(1);
    cache.series.resize(1);
    cache.series[0].type = "line";
    ASSERT_TRUE(model.apply_diff_op(ipc::make_figure_style_op(id, "talk")));
    ASSERT_TRUE(ipc::restyle_snapshot(cache.style, cache.axes, cache.series, "talk"));
    EXPECT_EQ(cache.series[0].line_width, style::find("talk")->line_width);
}

// ─── Export dialog plumbing ─────────────────────────────────────────────────

using ui::settings::ExportDialogSettings;
using ui::settings::ExportFormat;
using ui::settings::ExportSizeUnit;

TEST(ExportDialogSettings, PixelSizeDefaultsToFigureAndScales)
{
    ExportDialogSettings s;
    auto                 r = ui::settings::resolve_export_size(s, 800, 600);
    EXPECT_EQ(r.layout_width, 800u);
    EXPECT_EQ(r.layout_height, 600u);
    EXPECT_EQ(r.pixel_width, 800u);
    EXPECT_EQ(r.pixel_height, 600u);

    s.width_px  = 1000;
    s.height_px = 500;
    s.scale     = 2.0f;
    r           = ui::settings::resolve_export_size(s, 800, 600);
    EXPECT_EQ(r.layout_width, 1000u);
    EXPECT_EQ(r.pixel_width, 2000u);
    EXPECT_EQ(r.pixel_height, 1000u);
    EXPECT_FLOAT_EQ(r.pixel_scale, 2.0f);
}

TEST(ExportDialogSettings, InchesAndDpi)
{
    ExportDialogSettings s;
    s.unit       = ExportSizeUnit::Inches;
    s.width_in   = 6.0f;
    s.height_in  = 4.0f;
    s.dpi        = 300.0f;
    const auto r = ui::settings::resolve_export_size(s, 800, 600);
    EXPECT_EQ(r.layout_width, 576u);   // 6 in x 96 px/in
    EXPECT_EQ(r.layout_height, 384u);
    EXPECT_EQ(r.pixel_width, 1800u);   // 6 in x 300 dpi
    EXPECT_EQ(r.pixel_height, 1200u);
}

TEST(ExportDialogSettings, ClampsToTheExportLimit)
{
    ExportDialogSettings s;
    s.width_px   = 9000;
    s.height_px  = 9000;
    s.scale      = 4.0f;
    const auto r = ui::settings::resolve_export_size(s, 800, 600);
    EXPECT_LE(r.pixel_width, ui::settings::MAX_EXPORT_PIXELS);
    EXPECT_LE(r.pixel_height, ui::settings::MAX_EXPORT_PIXELS);
    EXPECT_EQ(r.layout_width, 9000u);

    ExportDialogSettings bad;
    bad.dpi       = -5.0f;
    bad.scale     = 1e9f;
    bad.theme     = "weird";
    const auto ok = ui::settings::sanitized(bad);
    EXPECT_GE(ok.dpi, 36.0f);
    EXPECT_LE(ok.scale, 8.0f);
    EXPECT_EQ(ok.theme, "current");
}

TEST(ExportDialogSettings, ThemeAndExtension)
{
    ExportDialogSettings s;
    EXPECT_EQ(ui::settings::export_theme_name(s), "");
    s.theme = "light";
    EXPECT_EQ(ui::settings::export_theme_name(s), "light");
    EXPECT_EQ(ui::settings::with_export_extension("/tmp/a.png", ExportFormat::Pdf), "/tmp/a.pdf");
    EXPECT_EQ(ui::settings::with_export_extension("/tmp/a", ExportFormat::Svg), "/tmp/a.svg");
    EXPECT_EQ(ui::settings::with_export_extension("/tmp.d/a", ExportFormat::Png), "/tmp.d/a.png");
}

TEST(ExportDialogSettings, RememberedInSettingsJson)
{
    const std::string path = temp_file("settings.json");
    {
        ui::settings::SettingsStore store;
        auto&                       e = store.data_mut().export_dialog;
        e.format                      = ExportFormat::Pdf;
        e.theme                       = "dark";
        e.style                       = "paper";
        e.unit                        = ExportSizeUnit::Inches;
        e.width_in                    = 3.5f;
        e.height_in                   = 2.5f;
        e.dpi                         = 600.0f;
        e.scale                       = 2.0f;
        e.width_px                    = 1234;
        e.transparent                 = true;
        ASSERT_TRUE(store.save(path));
    }
    ui::settings::SettingsStore loaded;
    ASSERT_TRUE(loaded.load(path));
    fs::remove(path);
    const auto& e = loaded.data().export_dialog;
    EXPECT_EQ(e.format, ExportFormat::Pdf);
    EXPECT_EQ(e.theme, "dark");
    EXPECT_EQ(e.style, "paper");
    EXPECT_EQ(e.unit, ExportSizeUnit::Inches);
    EXPECT_FLOAT_EQ(e.width_in, 3.5f);
    EXPECT_FLOAT_EQ(e.height_in, 2.5f);
    EXPECT_FLOAT_EQ(e.dpi, 600.0f);
    EXPECT_FLOAT_EQ(e.scale, 2.0f);
    EXPECT_EQ(e.width_px, 1234u);
    EXPECT_TRUE(e.transparent);
    // Other settings still load next to the new section.
    EXPECT_EQ(loaded.data().default_theme, ui::settings::SettingsData{}.default_theme);
}
