// Subplot grid spacing (FigureStyle margins / hgap / vgap), constrained layout,
// shared axes (sharex / sharey) and the figure suptitle.

#include <gtest/gtest.h>

#include <filesystem>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <string>
#include <vector>

#include "agent/snapshot_figure_builder.hpp"
#include "core/figure_layout.hpp"
#include "core/layout.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/axes_format_state.hpp"
#include "ipc/codec.hpp"
#include "ui/app/figure_snapshot.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;

namespace
{

const std::vector<float> X{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
const std::vector<float> Y{0.0f, 1.0f, 4.0f, 9.0f, 16.0f};

float right(const Rect& r)
{
    return r.x + r.w;
}

float bottom(const Rect& r)
{
    return r.y + r.h;
}

// 2x2 figure with labels and titles on every axes.
std::unique_ptr<Figure> labelled_grid(uint32_t w = 1000, uint32_t h = 700)
{
    auto fig = std::make_unique<Figure>(FigureConfig{.width = w, .height = h});
    for (int i = 1; i <= 4; ++i)
    {
        auto& ax = fig->subplot(2, 2, i);
        ax.plot(X, std::vector<float>{0.0f, 1000.0f, -2500.0f, 4000.0f, 12345.0f});
        ax.title("Panel " + std::to_string(i));
        ax.xlabel("time (s)");
        ax.ylabel("amplitude");
    }
    return fig;
}

std::string temp_path(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

}   // namespace

// ─── Grid math ──────────────────────────────────────────────────────────────

TEST(GridLayout, OuterMarginsAndGaps)
{
    GridSpacing sp;
    sp.outer     = Margins{10.0f, 20.0f, 30.0f, 40.0f};   // left right bottom top
    sp.col_gaps  = {50.0f};
    sp.row_gaps  = {60.0f};
    const auto r = compute_grid_layout(1030.0f, 800.0f, 2, 2, sp, 5.0f, 7.0f);
    ASSERT_EQ(r.size(), 4u);
    const float w = (1030.0f - 10.0f - 20.0f - 50.0f) / 2.0f;
    const float h = (800.0f - 30.0f - 40.0f - 60.0f) / 2.0f;
    EXPECT_FLOAT_EQ(r[0].x, 15.0f);
    EXPECT_FLOAT_EQ(r[0].y, 47.0f);
    EXPECT_FLOAT_EQ(r[0].w, w);
    EXPECT_FLOAT_EQ(r[0].h, h);
    EXPECT_FLOAT_EQ(r[1].x - right(r[0]), 50.0f);    // hgap between columns
    EXPECT_FLOAT_EQ(r[2].y - bottom(r[0]), 60.0f);   // vgap between rows
    EXPECT_FLOAT_EQ(right(r[3]), 5.0f + 1030.0f - 20.0f);
    EXPECT_FLOAT_EQ(bottom(r[3]), 7.0f + 800.0f - 30.0f);
}

TEST(GridLayout, MinSubplotHeightScrolls)
{
    GridSpacing sp;
    sp.outer           = Margins{0.0f, 0.0f, 10.0f, 10.0f};
    sp.row_gaps        = {20.0f, 20.0f};
    float      content = 0.0f;
    const auto r = compute_grid_layout(400.0f, 300.0f, 3, 1, sp, 0.0f, 0.0f, 150.0f, &content);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_FLOAT_EQ(r[0].h, 150.0f);
    EXPECT_FLOAT_EQ(content, 10.0f + 10.0f + 40.0f + 450.0f);
}

TEST(GridLayout, MissingGapsCountAsZeroAndSizesNeverNegative)
{
    GridSpacing sp;
    sp.outer     = Margins{300.0f, 300.0f, 300.0f, 300.0f};
    const auto r = compute_grid_layout(200.0f, 200.0f, 1, 3, sp);
    ASSERT_EQ(r.size(), 3u);
    for (const auto& c : r)
    {
        EXPECT_GE(c.w, 0.0f);
        EXPECT_GE(c.h, 0.0f);
    }
}

// ─── Manual layout: the style drives the grid exactly ───────────────────────

TEST(FigureLayout, ManualLayoutUsesStyleGapsAndMargins)
{
    Figure fig({.width = 1000, .height = 800});
    fig.subplot(2, 2, 1);
    fig.subplot(2, 2, 2);
    fig.subplot(2, 2, 3);
    fig.subplot(2, 2, 4);
    auto& st              = fig.style();
    st.constrained_layout = false;
    st.margin_left        = 100.0f;   // room for the y tick labels: no left growth
    st.margin_right       = 30.0f;
    st.margin_top         = 25.0f;
    st.margin_bottom      = 35.0f;
    st.subplot_hgap       = 120.0f;
    st.subplot_vgap       = 90.0f;
    fig.compute_layout();

    const Rect a = fig.axes()[0]->viewport();
    const Rect b = fig.axes()[1]->viewport();
    const Rect c = fig.axes()[2]->viewport();
    const Rect d = fig.axes()[3]->viewport();
    EXPECT_FLOAT_EQ(a.x, 100.0f);
    EXPECT_FLOAT_EQ(a.y, 25.0f);
    EXPECT_FLOAT_EQ(b.x - right(a), 120.0f);
    EXPECT_FLOAT_EQ(c.y - bottom(a), 90.0f);
    EXPECT_FLOAT_EQ(right(d), 1000.0f - 30.0f);
    EXPECT_FLOAT_EQ(bottom(d), 800.0f - 35.0f);

    // Inspector edits take effect on the next layout.
    st.subplot_vgap = 20.0f;
    fig.compute_layout();
    EXPECT_FLOAT_EQ(fig.axes()[2]->viewport().y - bottom(fig.axes()[0]->viewport()), 20.0f);
}

TEST(FigureLayout, ManualLayoutStillKeepsYTickLabelsOutOfTheNeighbour)
{
    Figure fig({.width = 1000, .height = 600});
    fig.subplot(1, 2, 1).plot(X, Y);
    fig.subplot(1, 2, 2).plot(X, std::vector<float>{0.0f, 1e5f, 2e5f, 3e5f, 4e5f});
    fig.subplot(1, 2, 2).ylabel("big values");
    fig.style().constrained_layout = false;
    fig.style().subplot_hgap       = 10.0f;
    fig.compute_layout();
    const Rect a = fig.axes()[0]->viewport();
    const Rect b = fig.axes()[1]->viewport();
    // The right plot shrinks from the left so its labels fit the 10 px gap + its cell.
    EXPECT_GE(b.x - left_label_extent(*fig.axes()[1]) + LEFT_LABEL_OUTER_PAD, right(a) - 0.5f);
}

// ─── Constrained layout ─────────────────────────────────────────────────────

TEST(FigureLayout, ConstrainedIsDefaultAndSingleAxesKeepsStyleMargins)
{
    Figure fig({.width = 800, .height = 600});
    fig.subplot(1, 1, 1).plot(X, Y);
    EXPECT_TRUE(fig.style().constrained_layout);
    fig.compute_layout();
    const Rect r = fig.axes()[0]->viewport();
    // Small labels fit the default margins: nothing grows.
    EXPECT_FLOAT_EQ(r.x, fig.style().margin_left);
    EXPECT_FLOAT_EQ(r.y, fig.style().margin_top);
    EXPECT_FLOAT_EQ(right(r), 800.0f - fig.style().margin_right);
    EXPECT_FLOAT_EQ(bottom(r), 600.0f - fig.style().margin_bottom);
}

TEST(FigureLayout, ConstrainedGapsFitTheLabelsWithoutOverlap)
{
    auto fig                  = labelled_grid();
    fig->style().subplot_hgap = 0.0f;   // let the labels alone decide
    fig->style().subplot_vgap = 0.0f;
    fig->compute_layout();
    const auto& ax = fig->axes();
    for (int row = 0; row < 2; ++row)
    {
        const Axes&           l  = *ax[row * 2];
        const Axes&           r  = *ax[row * 2 + 1];
        const AxesDecorations dl = measure_decorations(l);
        const AxesDecorations dr = measure_decorations(r);
        EXPECT_LE(right(l.viewport()) + dl.right, r.viewport().x - dr.left + 0.5f);
    }
    for (int col = 0; col < 2; ++col)
    {
        const Axes&           t  = *ax[col];
        const Axes&           b  = *ax[2 + col];
        const AxesDecorations dt = measure_decorations(t);
        const AxesDecorations db = measure_decorations(b);
        EXPECT_LE(bottom(t.viewport()) + dt.bottom, b.viewport().y - db.top + 0.5f);
        EXPECT_GT(db.top, 0.0f);       // the title is counted
        EXPECT_GT(dt.bottom, 30.0f);   // tick labels + x label
    }
    // Every label stays inside the figure.
    const Axes& tl = *ax[0];
    EXPECT_GE(tl.viewport().x - measure_decorations(tl).left, -0.5f);
    EXPECT_GE(tl.viewport().y - measure_decorations(tl).top, -0.5f);
}

TEST(FigureLayout, ConstrainedGapsAreAtLeastTheStyleGaps)
{
    Figure fig({.width = 1000, .height = 800});
    for (int i = 1; i <= 4; ++i)
        fig.subplot(2, 2, i);   // no labels
    fig.style().subplot_hgap = 150.0f;
    fig.style().subplot_vgap = 140.0f;
    fig.compute_layout();
    EXPECT_NEAR(fig.axes()[1]->viewport().x - right(fig.axes()[0]->viewport()), 150.0f, 0.01f);
    EXPECT_NEAR(fig.axes()[2]->viewport().y - bottom(fig.axes()[0]->viewport()), 140.0f, 0.01f);
}

TEST(FigureLayout, ConstrainedVerticalGapIsSmallerThanTheOldFixedSpacing)
{
    // The old layout put margin_top + margin_bottom (100 px) between rows even
    // without titles; constrained layout only takes what the labels need.
    Figure fig({.width = 1000, .height = 800});
    fig.subplot(2, 1, 1).plot(X, Y);
    fig.subplot(2, 1, 2).plot(X, Y);
    fig.compute_layout();
    const float gap = fig.axes()[1]->viewport().y - bottom(fig.axes()[0]->viewport());
    EXPECT_LT(gap, 100.0f);
    EXPECT_GE(gap, fig.style().subplot_vgap - 0.01f);
}

TEST(FigureLayout, SpacingGrowthIsCappedOnTinyFigures)
{
    auto fig = labelled_grid(160, 120);
    fig->compute_layout();
    for (const auto& a : fig->axes())
    {
        EXPECT_GT(a->viewport().w, 0.0f);
        EXPECT_GT(a->viewport().h, 0.0f);
    }
}

TEST(FigureLayout, TightLayoutTurnsConstrainedLayoutOn)
{
    Figure fig;
    fig.style().constrained_layout = false;
    fig.tight_layout();
    EXPECT_TRUE(fig.style().constrained_layout);
}

// ─── Suptitle ───────────────────────────────────────────────────────────────

TEST(FigureLayout, SuptitleTakesABandAboveTheGrid)
{
    Figure fig({.width = 900, .height = 600});
    fig.subplot(1, 2, 1).plot(X, Y);
    fig.subplot(1, 2, 2).plot(X, Y);
    fig.compute_layout();
    const float top_without = fig.axes()[0]->viewport().y;
    EXPECT_FLOAT_EQ(fig.suptitle_rect().w, 0.0f);

    fig.suptitle("Overview");
    EXPECT_EQ(fig.suptitle(), "Overview");
    fig.compute_layout();
    const Rect  s = fig.suptitle_rect();
    const float b = suptitle_band_height(fig);
    EXPECT_GT(b, 20.0f);
    EXPECT_NEAR(fig.axes()[0]->viewport().y, top_without + b, 0.01f);
    // Centred over the grid, above the plots.
    EXPECT_NEAR(s.x + s.w * 0.5f,
                (fig.axes()[0]->viewport().x + right(fig.axes()[1]->viewport())) * 0.5f,
                40.0f);
    EXPECT_LT(s.y + s.h, fig.axes()[0]->viewport().y);
    EXPECT_GE(s.y, 0.0f);

    fig.suptitle("");
    fig.compute_layout();
    EXPECT_FLOAT_EQ(fig.suptitle_rect().w, 0.0f);
    EXPECT_NEAR(fig.axes()[0]->viewport().y, top_without, 0.01f);
}

// ─── Shared axes ────────────────────────────────────────────────────────────

TEST(SharedAxes, AutoscaleFitsTheWholeGroup)
{
    Axes a;
    Axes b;
    a.plot(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f});
    b.plot(std::vector<float>{10.0f, 20.0f}, std::vector<float>{5.0f, 6.0f});
    a.sharex(b);
    EXPECT_TRUE(a.x_shared());
    EXPECT_TRUE(b.x_shared());
    EXPECT_FALSE(a.y_shared());
    EXPECT_DOUBLE_EQ(a.x_limits().min, b.x_limits().min);
    EXPECT_DOUBLE_EQ(a.x_limits().max, b.x_limits().max);
    EXPECT_LT(a.x_limits().min, 0.0);
    EXPECT_GT(a.x_limits().max, 20.0);
    // Y stays per-axes.
    EXPECT_LT(a.y_limits().max, 2.0);
    EXPECT_GT(b.y_limits().min, 4.0);
}

TEST(SharedAxes, LimitsPropagateAndAutoFitResetsAll)
{
    Axes a;
    Axes b;
    Axes c;
    a.plot(X, Y);
    b.plot(X, Y);
    c.plot(X, Y);
    b.sharey(a);
    c.sharey(a);
    b.ylim(-3.0, 3.0);
    EXPECT_DOUBLE_EQ(a.y_limits().min, -3.0);
    EXPECT_DOUBLE_EQ(c.y_limits().max, 3.0);
    c.auto_fit();
    EXPECT_GT(a.y_limits().max, 15.0);
    EXPECT_GT(b.y_limits().max, 15.0);
    a.clear_ylim();
    EXPECT_EQ(a.shared_y_axes().size(), 2u);
}

TEST(SharedAxes, JoiningMergesExplicitLimits)
{
    Axes a;
    Axes b;
    a.xlim(0.0, 5.0);
    b.xlim(3.0, 9.0);
    a.sharex(b);
    EXPECT_DOUBLE_EQ(a.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(a.x_limits().max, 9.0);
    EXPECT_DOUBLE_EQ(b.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(b.x_limits().max, 9.0);
}

TEST(SharedAxes, UnshareAndDestructionLeaveTheOthersConsistent)
{
    Axes a;
    Axes b;
    {
        Axes c;
        b.sharex(a);
        c.sharex(a);
        EXPECT_EQ(a.shared_x_axes().size(), 2u);
    }   // c destroyed: leaves the group
    EXPECT_EQ(a.shared_x_axes().size(), 1u);
    b.unshare_x();
    EXPECT_FALSE(a.x_shared());
    EXPECT_FALSE(b.x_shared());
    a.xlim(1.0, 2.0);
    EXPECT_NE(b.x_limits().min, 1.0);
}

TEST(SharedAxes, FigureShareModesGroupByRowAndColumn)
{
    Figure fig({.width = 900, .height = 700});
    fig.share_x(ShareAxes::Col);
    fig.share_y(ShareAxes::Row);
    for (int i = 1; i <= 4; ++i)
        fig.subplot(2, 2, i).plot(X, Y);   // axes created after the mode join too
    const auto& ax = fig.axes();
    EXPECT_EQ(ax[0]->shared_x_axes(), std::vector<Axes*>{ax[2].get()});
    EXPECT_EQ(ax[1]->shared_x_axes(), std::vector<Axes*>{ax[3].get()});
    EXPECT_EQ(ax[0]->shared_y_axes(), std::vector<Axes*>{ax[1].get()});
    EXPECT_EQ(ax[2]->shared_y_axes(), std::vector<Axes*>{ax[3].get()});

    ax[0]->xlim(-1.0, 1.0);
    EXPECT_DOUBLE_EQ(ax[2]->x_limits().max, 1.0);
    EXPECT_NE(ax[1]->x_limits().max, 1.0);

    fig.share_x(ShareAxes::All);
    EXPECT_EQ(ax[3]->shared_x_axes().size(), 3u);
    fig.share_x(ShareAxes::None);
    EXPECT_FALSE(ax[3]->x_shared());
}

TEST(SharedAxes, InnerTickLabelsAreHidden)
{
    Figure fig({.width = 900, .height = 700});
    for (int i = 1; i <= 4; ++i)
        fig.subplot(2, 2, i).plot(X, Y);
    fig.share_x(ShareAxes::All);
    fig.share_y(ShareAxes::All);
    fig.compute_layout();
    const auto& ax = fig.axes();
    EXPECT_FALSE(ax[0]->x_tick_labels_visible());   // top row
    EXPECT_FALSE(ax[1]->x_tick_labels_visible());
    EXPECT_TRUE(ax[2]->x_tick_labels_visible());   // bottom row
    EXPECT_TRUE(ax[3]->x_tick_labels_visible());
    EXPECT_TRUE(ax[0]->y_tick_labels_visible());   // left column
    EXPECT_FALSE(ax[1]->y_tick_labels_visible());
    EXPECT_TRUE(ax[2]->y_tick_labels_visible());
    EXPECT_FALSE(ax[3]->y_tick_labels_visible());

    // Hidden labels need no room: the shared grid packs tighter.
    const float vgap = ax[2]->viewport().y - bottom(ax[0]->viewport());
    fig.share_x(ShareAxes::None);
    fig.share_y(ShareAxes::None);
    fig.compute_layout();
    EXPECT_TRUE(ax[0]->x_tick_labels_visible());
    EXPECT_TRUE(ax[1]->y_tick_labels_visible());
    EXPECT_LE(vgap, ax[2]->viewport().y - bottom(ax[0]->viewport()) + 0.01f);
}

// ─── Persistence and IPC ────────────────────────────────────────────────────

namespace
{

std::unique_ptr<Figure> layout_figure_fixture()
{
    auto fig = std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480});
    for (int i = 1; i <= 4; ++i)
        fig->subplot(2, 2, i).plot(X, Y);
    fig->suptitle("Run 42");
    fig->share_x(ShareAxes::Col);
    fig->share_y(ShareAxes::All);
    auto& st              = fig->style();
    st.constrained_layout = false;
    st.subplot_hgap       = 33.0f;
    st.subplot_vgap       = 44.0f;
    st.margin_left        = 81.0f;
    return fig;
}

void expect_layout_restored(const Figure& fig, bool with_style)
{
    EXPECT_EQ(fig.suptitle(), "Run 42");
    EXPECT_EQ(fig.share_x(), ShareAxes::Col);
    EXPECT_EQ(fig.share_y(), ShareAxes::All);
    EXPECT_FALSE(fig.style().constrained_layout);
    ASSERT_EQ(fig.axes().size(), 4u);
    EXPECT_EQ(fig.axes()[0]->shared_x_axes(), std::vector<Axes*>{fig.axes()[2].get()});
    EXPECT_EQ(fig.axes()[0]->shared_y_axes().size(), 3u);
    if (with_style)
    {
        EXPECT_FLOAT_EQ(fig.style().subplot_hgap, 33.0f);
        EXPECT_FLOAT_EQ(fig.style().subplot_vgap, 44.0f);
        EXPECT_FLOAT_EQ(fig.style().margin_left, 81.0f);
    }
}

}   // namespace

TEST(SubplotLayoutPersistence, BinaryFigureFileRoundTrip)
{
    auto              fig  = layout_figure_fixture();
    const std::string path = temp_path("spectra_subplot_layout_fig.spectra");
    ASSERT_TRUE(FigureSerializer::save(path, *fig));
    Figure loaded({.width = 640, .height = 480});
    ASSERT_TRUE(FigureSerializer::load(path, loaded));
    expect_layout_restored(loaded, true);
    std::filesystem::remove(path);
}

TEST(SubplotLayoutPersistence, WorkspaceJsonRoundTrip)
{
    auto                 fig = layout_figure_fixture();
    std::vector<Figure*> figs{fig.get()};
    const auto           data = Workspace::capture(figs, 0, "night", true, 320.0f, false);
    const std::string    path = temp_path("spectra_subplot_layout_ws.spectra");
    ASSERT_TRUE(Workspace::save(path, data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].has_layout);

    Figure fresh({.width = 640, .height = 480});
    for (int i = 1; i <= 4; ++i)
        fresh.subplot(2, 2, i).plot(X, Y);
    std::vector<Figure*> targets{&fresh};
    ASSERT_TRUE(Workspace::apply(loaded, targets));
    expect_layout_restored(fresh, true);
    std::filesystem::remove(path);
}

TEST(SubplotLayoutTransport, SnapshotRoundTripRebuildsTheWindowFigure)
{
    auto                      fig = layout_figure_fixture();
    ipc::StateSnapshotPayload p;
    p.figures.push_back(figure_to_snapshot(*fig, 3));
    auto back = ipc::decode_state_snapshot(ipc::encode_state_snapshot(p));
    ASSERT_TRUE(back.has_value());
    const auto& s = back->figures.at(0);
    EXPECT_EQ(s.suptitle, "Run 42");
    EXPECT_EQ(s.constrained_layout, 0);
    EXPECT_EQ(s.share_x, static_cast<uint8_t>(ShareAxes::Col));

    auto live = agent::build_figure_from_snapshot(s);
    ASSERT_TRUE(live);
    expect_layout_restored(*live, false);
}

TEST(SubplotLayoutTransport, OlderSnapshotsKeepDefaults)
{
    ipc::StateSnapshotPayload p;
    p.figures.emplace_back();
    auto back = ipc::decode_state_snapshot(ipc::encode_state_snapshot(p));
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->figures[0].suptitle.empty());
    EXPECT_EQ(back->figures[0].constrained_layout, 1);
    EXPECT_EQ(back->figures[0].share_x, 0);
}

TEST(SubplotLayoutTransport, DiffOpsUpdateTheDaemonModel)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid);

    ipc::StateDiffPayload d;
    d.ops.push_back(model.set_figure_suptitle(fid, "Hello"));
    d.ops.push_back(model.set_figure_layout(fid, false, ShareAxes::Row, ShareAxes::Col));
    auto back = ipc::decode_state_diff(ipc::encode_state_diff(d));
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->ops.size(), 2u);
    EXPECT_EQ(back->ops[0].type, ipc::DiffOp::Type::SET_FIGURE_SUPTITLE);
    EXPECT_EQ(back->ops[1].type, ipc::DiffOp::Type::SET_FIGURE_LAYOUT);

    const auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    EXPECT_EQ(snap.figures[0].suptitle, "Hello");
    EXPECT_EQ(snap.figures[0].constrained_layout, 0);
    EXPECT_EQ(snap.figures[0].share_x, static_cast<uint8_t>(ShareAxes::Row));
    EXPECT_EQ(snap.figures[0].share_y, static_cast<uint8_t>(ShareAxes::Col));

    // Applying the ops (as a window does) gives the same state.
    ipc::SnapshotFigureState fs;
    for (const auto& op : back->ops)
        ipc::apply_figure_layout_op(fs, op);
    EXPECT_EQ(fs.suptitle, "Hello");
    EXPECT_EQ(fs.share_y, static_cast<uint8_t>(ShareAxes::Col));
    EXPECT_EQ(ipc::share_axes_from_wire(99), ShareAxes::None);
    EXPECT_EQ(ipc::share_axes_from_wire(-1), ShareAxes::None);
}
