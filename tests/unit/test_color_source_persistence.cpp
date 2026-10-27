// Issue #104: palette and format-letter colours are resolved per theme at draw
// time.  Saving, reloading, pasting and duplicating a series must keep the
// colour *source* (palette slot / format letter / explicit RGB) instead of
// freezing the RGB that was resolved last; old files keep loading.  Python's
// named letter colours travel as letters through the daemon.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "daemon/figure_model.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;

namespace
{

const std::vector<float> kX = {0.0f, 1.0f, 2.0f};
const std::vector<float> kY = {1.0f, 3.0f, 2.0f};

bool same_rgb(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// A light-theme resolved colour, standing in for whatever the last draw set.
const Color kResolvedPalette{0.12f, 0.47f, 0.71f, 1.0f};

// Figure with an auto series (slot 2, already resolved for a light theme), a
// letter series ('k', light shade) and an explicit series.
void populate(Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.line(kX, kY);   // slot 0
    ax.line(kX, kY);   // slot 1
    auto& autos = ax.line(kX, kY).label("auto");
    autos.resolve_auto_color(kResolvedPalette);
    auto& letter = ax.plot(kX, kY, "k--").label("letter");
    letter.resolve_letter_color(format_colors::shade('k', true));
    ax.scatter(kX, kY).label("explicit").color(Color{0.3f, 0.6f, 0.9f, 1.0f});
}

void expect_sources(const Figure& fig)
{
    ASSERT_EQ(fig.axes().size(), 1u);
    const auto& series = fig.axes()[0]->series();
    ASSERT_EQ(series.size(), 5u);

    const Series& autos = *series[2];
    EXPECT_TRUE(autos.has_auto_color());
    EXPECT_EQ(autos.auto_color_index(), 2);
    EXPECT_TRUE(same_rgb(autos.color(), kResolvedPalette));   // provisional = last resolved

    const Series& letter = *series[3];
    EXPECT_FALSE(letter.has_auto_color());
    EXPECT_EQ(letter.color_letter(), 'k');

    const Series& expl = *series[4];
    EXPECT_FALSE(expl.has_auto_color());
    EXPECT_EQ(expl.color_letter(), '\0');
    EXPECT_TRUE(same_rgb(expl.color(), Color{0.3f, 0.6f, 0.9f, 1.0f}));
}

class TempFile
{
   public:
    explicit TempFile(const char* name)
        : path_((std::filesystem::temp_directory_path() / name).string())
    {
    }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

   private:
    std::string path_;
};

}   // namespace

// ─── Binary figure file (.spectra) ──────────────────────────────────────────

TEST(ColorSourcePersistence, FigureFileKeepsPaletteLetterAndExplicit)
{
    TempFile tmp("spectra_color_source_fig.spectra");
    Figure   src;
    populate(src);
    ASSERT_TRUE(FigureSerializer::save(tmp.path(), src));

    Figure dst;
    ASSERT_TRUE(FigureSerializer::load(tmp.path(), dst));
    expect_sources(dst);
}

TEST(ColorSourcePersistence, FigureFileKeepsSourceFor3DAndStatSeries)
{
    TempFile tmp("spectra_color_source_3d.spectra");
    Figure   src;
    auto&    ax  = src.subplot(1, 2, 1);
    auto&    bar = ax.bar(kX, kY);
    auto&    ax3 = src.subplot3d(1, 2, 2);
    auto&    l3  = ax3.line3d(kX, kY, kX);
    ASSERT_TRUE(bar.has_auto_color());
    ASSERT_TRUE(l3.has_auto_color());
    ASSERT_TRUE(FigureSerializer::save(tmp.path(), src));

    Figure dst;
    ASSERT_TRUE(FigureSerializer::load(tmp.path(), dst));
    ASSERT_FALSE(dst.axes().empty());
    ASSERT_FALSE(dst.axes()[0]->series().empty());
    EXPECT_TRUE(dst.axes()[0]->series()[0]->has_auto_color());
    bool found_3d = false;
    for (const auto& a : dst.all_axes())
        if (auto* a3 = dynamic_cast<Axes3D*>(a.get()); a3 && !a3->series().empty())
        {
            found_3d = true;
            EXPECT_TRUE(a3->series()[0]->has_auto_color());
        }
    EXPECT_TRUE(found_3d);
}

TEST(ColorSourcePersistence, NotchedBoxPlotKeepsSampleCount)
{
    TempFile                 tmp("spectra_color_source_box.spectra");
    Figure                   src;
    auto&                    ax = src.subplot(1, 1, 1);
    auto&                    bp = ax.box_plot();
    const std::vector<float> values{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    bp.add_box(1.0f, values).notched(true);
    ASSERT_EQ(bp.stats()[0].count, values.size());
    ASSERT_TRUE(FigureSerializer::save(tmp.path(), src));

    Figure dst;
    ASSERT_TRUE(FigureSerializer::load(tmp.path(), dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    auto* loaded = dynamic_cast<BoxPlotSeries*>(dst.axes()[0]->series()[0].get());
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->notched());
    ASSERT_EQ(loaded->stats().size(), 1u);
    EXPECT_EQ(loaded->stats()[0].count, values.size());
    EXPECT_FLOAT_EQ(loaded->stats()[0].median, bp.stats()[0].median);
    EXPECT_TRUE(loaded->has_auto_color());
}

namespace
{
// Minimal writer for hand-built files in the previous (v6) layout.
struct Bytes
{
    std::vector<char> b;
    template <typename T>
    void put(T v)
    {
        const char* p = reinterpret_cast<const char*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
    }
    void str(const std::string& s)
    {
        put<uint32_t>(static_cast<uint32_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void floats(const std::vector<float>& v)
    {
        put<uint32_t>(static_cast<uint32_t>(v.size()));
        for (float f : v)
            put<float>(f);
    }
    void color(const Color& c)
    {
        put(c.r);
        put(c.g);
        put(c.b);
        put(c.a);
    }
    void chunk(uint16_t tag, const Bytes& body)
    {
        put<uint16_t>(tag);
        put<uint32_t>(static_cast<uint32_t>(body.b.size()));
        b.insert(b.end(), body.b.begin(), body.b.end());
    }
    void series_common(const std::string& label, const Color& c)
    {
        str(label);
        color(c);
        put<uint8_t>(1);    // visible
        put<uint8_t>(1);    // line style
        put<uint8_t>(0);    // marker style
        put<float>(6.0f);   // marker size
        put<float>(1.0f);   // opacity
        put<float>(2.0f);   // line width
        put<uint8_t>(0);    // excluded from autoscale
        put<uint8_t>(1);    // in legend
        put<uint8_t>(0);    // reference line
    }
};
}   // namespace

TEST(ColorSourcePersistence, Version6FileStillLoadsWithExplicitColours)
{
    TempFile    tmp("spectra_color_source_v6.spectra");
    const Color line_col{0.2f, 0.4f, 0.6f, 1.0f};
    const Color box_col{0.7f, 0.1f, 0.3f, 1.0f};

    Bytes axes;
    axes.put<int32_t>(0);
    axes.str("t");
    axes.str("x");
    axes.str("y");
    axes.put<uint8_t>(1);   // grid
    axes.put<uint8_t>(1);   // border
    axes.put<uint8_t>(0);   // autoscale mode
    for (float v : {0.0f, 2.0f, 0.0f, 3.0f})
        axes.put<float>(v);
    for (int i = 0; i < 3; ++i)
        axes.color(colors::gray);
    for (float v : {5.0f, 12.0f, 14.0f, 1.0f})
        axes.put<float>(v);
    for (double v : {0.0, 2.0, 0.0, 3.0})
        axes.put<double>(v);

    Bytes line;
    line.put<int32_t>(0);
    line.series_common("old line", line_col);
    line.put<float>(2.0f);
    line.floats(kX);
    line.floats(kY);
    line.put<double>(0.0);
    line.put<double>(0.0);

    Bytes box;
    box.put<int32_t>(0);
    box.series_common("old box", box_col);
    box.put<float>(0.6f);
    box.put<uint8_t>(1);   // show outliers
    box.put<uint8_t>(1);   // notched
    box.put<uint8_t>(0);   // gradient
    box.put<uint32_t>(1);
    for (float v : {1.0f, 5.0f, 3.0f, 7.0f, 1.0f, 9.0f})
        box.put<float>(v);
    box.floats({});

    Bytes file;
    file.put<uint32_t>(0x53504346);   // "SPCF"
    file.put<uint32_t>(6);
    file.chunk(0x0010, axes);
    file.chunk(0x0020, line);
    file.chunk(0x0026, box);
    file.put<uint16_t>(0xFFFF);
    file.put<uint32_t>(0);
    {
        std::FILE* f = std::fopen(tmp.path().c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fwrite(file.b.data(), 1, file.b.size(), f);
        std::fclose(f);
    }

    Figure dst;
    ASSERT_TRUE(FigureSerializer::load(tmp.path(), dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    const auto& series = dst.axes()[0]->series();
    ASSERT_EQ(series.size(), 2u);
    EXPECT_EQ(series[0]->label(), "old line");
    EXPECT_FALSE(series[0]->has_auto_color());
    EXPECT_TRUE(same_rgb(series[0]->color(), line_col));
    auto* bp = dynamic_cast<const BoxPlotSeries*>(series[1].get());
    ASSERT_NE(bp, nullptr);
    EXPECT_TRUE(same_rgb(bp->color(), box_col));
    ASSERT_EQ(bp->stats().size(), 1u);
    EXPECT_FLOAT_EQ(bp->stats()[0].median, 5.0f);
    EXPECT_EQ(bp->stats()[0].count, 0u);   // unknown in v6
}

// ─── JSON workspace ─────────────────────────────────────────────────────────

TEST(ColorSourcePersistence, WorkspaceKeepsColorSource)
{
    TempFile             tmp("spectra_color_source_ws.spectra");
    Figure               src;
    std::vector<Figure*> figs{&src};
    populate(src);
    auto data = Workspace::capture(figs, 0, "light", true, 320.0f, false);
    ASSERT_TRUE(Workspace::save(tmp.path(), data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp.path(), loaded));
    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].series.size(), 5u);
    EXPECT_EQ(loaded.figures[0].series[2].color_source, "auto");
    EXPECT_EQ(loaded.figures[0].series[2].color_index, 2);
    EXPECT_EQ(loaded.figures[0].series[3].color_source, "letter");
    EXPECT_EQ(loaded.figures[0].series[3].color_letter, "k");
    EXPECT_EQ(loaded.figures[0].series[4].color_source, "rgb");

    // Apply onto a figure whose series were all recoloured explicitly.
    Figure dst;
    populate(dst);
    for (auto& s : dst.axes()[0]->series_mut())
        s->color(colors::red);
    std::vector<Figure*> dst_figs{&dst};
    ASSERT_TRUE(Workspace::apply(loaded, dst_figs));
    expect_sources(dst);
}

TEST(ColorSourcePersistence, OldWorkspaceWithoutSourceLeavesColoursAlone)
{
    WorkspaceData data;
    data.figures.resize(1);
    data.figures[0].series.resize(1);   // no color_source (pre-#104 file)
    data.figures[0].series[0].color_r = 0.0f;

    Figure dst;
    auto&  s = dst.subplot(1, 1, 1).line(kX, kY);
    ASSERT_TRUE(s.has_auto_color());
    std::vector<Figure*> figs{&dst};
    ASSERT_TRUE(Workspace::apply(data, figs));
    EXPECT_TRUE(s.has_auto_color());
}

// ─── Clipboard / duplicate ──────────────────────────────────────────────────

TEST(ColorSourcePersistence, PasteKeepsPaletteAndLetterSources)
{
    Figure src;
    populate(src);
    Figure dst;
    auto&  target = dst.subplot(1, 1, 1);

    const auto& series = src.axes()[0]->series();
    Series*     pa     = SeriesClipboard::paste_to(target, SeriesClipboard::snapshot(*series[2]));
    Series*     pl     = SeriesClipboard::paste_to(target, SeriesClipboard::snapshot(*series[3]));
    Series*     pe     = SeriesClipboard::paste_to(target, SeriesClipboard::snapshot(*series[4]));
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pl, nullptr);
    ASSERT_NE(pe, nullptr);

    EXPECT_TRUE(pa->has_auto_color());
    EXPECT_EQ(pa->auto_color_index(), 2);
    EXPECT_TRUE(same_rgb(pa->color(), kResolvedPalette));
    EXPECT_EQ(pl->color_letter(), 'k');
    EXPECT_FALSE(pe->has_auto_color());
    EXPECT_EQ(pe->color_letter(), '\0');
}

TEST(ColorSourcePersistence, DuplicateFigureKeepsColorSources)
{
    FigureRegistry registry;
    auto           fig = std::make_unique<Figure>();
    populate(*fig);
    const FigureId src_id = registry.register_figure(std::move(fig));

    FigureManager  mgr(registry);
    const FigureId dup_id = mgr.duplicate_figure(src_id);
    Figure*        dup    = registry.get(dup_id);
    ASSERT_NE(dup, nullptr);
    expect_sources(*dup);
}

// ─── Daemon: Python named colours as letters ────────────────────────────────

TEST(ColorSourcePersistence, FigureModelColorLetterKeepsLetter)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t idx = 0;
    model.add_series_with_diff(fid, "a", "line", 0, idx);

    const ipc::DiffOp op = model.set_series_color_letter(fid, idx, 'k');
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_SERIES_COLOR);
    EXPECT_EQ(op.str_val, "k");
    // Old windows that ignore str_val still get a sensible (dark-theme) shade.
    EXPECT_FLOAT_EQ(static_cast<float>(op.f1), format_colors::shade('k').r);

    auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    ASSERT_EQ(snap.figures[0].series.size(), 1u);
    EXPECT_EQ(snap.figures[0].series[0].color_letter, static_cast<uint8_t>('k'));
    EXPECT_EQ(snap.figures[0].series[0].color_index, -1);

    // Replaying the op (agent-side model) keeps the letter; a plain RGB op clears it.
    daemon::FigureModel replica;
    const auto          rid = replica.create_figure("f");
    replica.add_axes(rid);
    uint32_t ridx = 0;
    replica.add_series_with_diff(rid, "a", "line", 0, ridx);
    ipc::DiffOp replay = op;
    replay.figure_id   = rid;
    ASSERT_TRUE(replica.apply_diff_op(replay));
    EXPECT_EQ(replica.snapshot().figures[0].series[0].color_letter, static_cast<uint8_t>('k'));
    replica.set_series_color(rid, ridx, 1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(replica.snapshot().figures[0].series[0].color_letter, 0);
}

TEST(ColorSourcePersistence, FigureModelIgnoresNonLetter)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t idx = 0;
    model.add_series_with_diff(fid, "a", "line", 0, idx);
    model.set_series_color_letter(fid, idx, 'q');
    auto snap = model.snapshot();
    EXPECT_EQ(snap.figures[0].series[0].color_letter, 0);
    EXPECT_EQ(snap.figures[0].series[0].color_index, 0);   // still auto
}
