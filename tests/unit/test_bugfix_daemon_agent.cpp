// Regression tests for daemon fixes: series removal / topic re-indexing,
// subplot grid tracking, bounded topic streaming and topic stride validation.

#include <gtest/gtest.h>

#include <vector>

#include "daemon/figure_model.hpp"
#include "daemon/topic_registry.hpp"

using spectra::daemon::FigureModel;
using spectra::daemon::TopicRegistry;
using spectra::daemon::TopicSubscription;
namespace ipc = spectra::ipc;

// ─── FigureModel::remove_series ─────────────────────────────────────────────

TEST(BugfixDaemonAgent, RemoveSeriesInRangeShiftsAndBumpsRevision)
{
    FigureModel model;
    auto        fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t a = 0;
    uint32_t b = 0;
    model.add_series_with_diff(fid, "a", "line", 0, a);
    model.add_series_with_diff(fid, "b", "line", 0, b);
    ASSERT_EQ(a, 0u);
    ASSERT_EQ(b, 1u);

    auto rev     = model.revision();
    bool removed = false;
    auto op      = model.remove_series(fid, 0, &removed);
    EXPECT_TRUE(removed);
    EXPECT_EQ(op.type, ipc::DiffOp::Type::REMOVE_SERIES);
    EXPECT_EQ(op.series_index, 0u);
    EXPECT_GT(model.revision(), rev);

    auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    ASSERT_EQ(snap.figures[0].series.size(), 1u);
    EXPECT_EQ(snap.figures[0].series[0].name, "b");
}

TEST(BugfixDaemonAgent, RemoveSeriesOutOfRangeIsNoOp)
{
    FigureModel model;
    auto        fid = model.create_figure("f");
    model.add_series(fid, "a", "line");

    auto rev     = model.revision();
    bool removed = true;
    model.remove_series(fid, 5, &removed);
    EXPECT_FALSE(removed);
    EXPECT_EQ(model.revision(), rev);

    removed = true;
    model.remove_series(fid + 100, 0, &removed);
    EXPECT_FALSE(removed);
    EXPECT_EQ(model.revision(), rev);

    // Default argument keeps the old call form source-compatible.
    model.remove_series(fid, 9);
    EXPECT_EQ(model.revision(), rev);
}

// ─── FigureModel::get_grid ──────────────────────────────────────────────────

TEST(BugfixDaemonAgent, GetGridReflectsGrownGrid)
{
    FigureModel model;
    auto        fid  = model.create_figure("f");
    int32_t     rows = 0;
    int32_t     cols = 0;
    ASSERT_TRUE(model.get_grid(fid, rows, cols));
    EXPECT_EQ(rows, 1);
    EXPECT_EQ(cols, 1);

    model.set_grid(fid, 2, 1);
    ASSERT_TRUE(model.get_grid(fid, rows, cols));
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(cols, 1);

    EXPECT_FALSE(model.get_grid(fid + 1, rows, cols));
}

// ─── FigureModel::append_series_data rolling window ─────────────────────────

TEST(BugfixDaemonAgent, AppendSeriesDataUnboundedByDefault)
{
    FigureModel model;
    auto        fid = model.create_figure("f");
    uint32_t    si  = 0;
    model.add_series_with_diff(fid, "s", "line", 0, si);

    for (int i = 0; i < 10; ++i)
        model.append_series_data(fid, si, {float(i), float(i)});
    auto op = model.append_series_data(fid, si, {10.0f, 10.0f});
    EXPECT_EQ(op.data.size(), 22u);
}

TEST(BugfixDaemonAgent, AppendSeriesDataTrimsToMaxPoints2D)
{
    FigureModel model;
    auto        fid = model.create_figure("f");
    uint32_t    si  = 0;
    model.add_series_with_diff(fid, "s", "line", 0, si);

    ipc::DiffOp op;
    for (int i = 0; i < 10; ++i)
        op = model.append_series_data(fid, si, {float(i), float(i * 10)}, 4);

    ASSERT_EQ(op.data.size(), 8u);
    // Newest four points, still x/y aligned.
    std::vector<float> expected = {6, 60, 7, 70, 8, 80, 9, 90};
    EXPECT_EQ(op.data, expected);

    auto snap = model.snapshot();
    EXPECT_EQ(snap.figures[0].series[0].point_count, 4u);
    EXPECT_EQ(snap.figures[0].series[0].data, expected);
}

TEST(BugfixDaemonAgent, AppendSeriesDataTrimsToMaxPoints3D)
{
    FigureModel model;
    auto        fid = model.create_figure("f");
    uint32_t    si  = 0;
    model.add_series_with_diff(fid, "s", "line3d", 0, si);

    // One call with 5 points, window of 2.
    auto op = model.append_series_data(fid, si, {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4}, 2);
    std::vector<float> expected = {3, 3, 3, 4, 4, 4};
    EXPECT_EQ(op.data, expected);
}

// ─── TopicRegistry::on_series_removed ───────────────────────────────────────

TEST(BugfixDaemonAgent, SeriesRemovalReindexesTopicSubscriptions)
{
    TopicRegistry reg;
    reg.declare("t0", ipc::TopicKind::Scalar2D, "", 16, 1);
    reg.declare("t1", ipc::TopicKind::Scalar2D, "", 16, 1);
    reg.declare("other", ipc::TopicKind::Scalar2D, "", 16, 1);

    ASSERT_TRUE(reg.subscribe("t0", TopicSubscription{7, 0, 0}));
    ASSERT_TRUE(reg.subscribe("t1", TopicSubscription{7, 0, 1}));
    ASSERT_TRUE(reg.subscribe("other", TopicSubscription{8, 0, 1}));   // other figure

    reg.on_series_removed(7, 0);

    std::vector<TopicSubscription> subs;
    ASSERT_TRUE(reg.publish("t0", {1.0, 2.0}, nullptr, &subs));
    EXPECT_TRUE(subs.empty());   // removed series no longer receives data

    ASSERT_TRUE(reg.publish("t1", {1.0, 2.0}, nullptr, &subs));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].figure_id, 7u);
    EXPECT_EQ(subs[0].series_index, 0u);   // shifted down with the model

    ASSERT_TRUE(reg.publish("other", {1.0, 2.0}, nullptr, &subs));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].series_index, 1u);   // different figure untouched
}

// ─── TopicRegistry stride validation ────────────────────────────────────────

TEST(BugfixDaemonAgent, PublishRejectsMisalignedSamples)
{
    TopicRegistry reg;
    reg.declare("imu", ipc::TopicKind::Scalar2D, "", 16, 1);

    EXPECT_FALSE(reg.publish("imu", {1.0, 2.0, 3.0}, nullptr, nullptr));
    ASSERT_TRUE(reg.publish("imu", {1.0, 2.0}, nullptr, nullptr));

    std::vector<double> retained;
    ASSERT_TRUE(reg.subscribe("imu", TopicSubscription{1, 0, 0}, nullptr, &retained));
    EXPECT_EQ(retained, (std::vector<double>{1.0, 2.0}));

    auto infos = reg.snapshot();
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].total_samples, 1u);
}

TEST(BugfixDaemonAgent, RedeclareWithDifferentKindDropsRing)
{
    TopicRegistry reg;
    reg.declare("pos", ipc::TopicKind::Scalar2D, "", 16, 1);
    ASSERT_TRUE(reg.publish("pos", {1.0, 2.0, 3.0, 4.0}, nullptr, nullptr));
    reg.on_client_disconnect(1);

    // Reclaimed by a new publisher as a 3D topic: old 2D samples must not be
    // replayed with a stride of 3.
    EXPECT_EQ(reg.declare("pos", ipc::TopicKind::Scalar3D, "", 16, 2),
              TopicRegistry::DeclareResult::ReclaimedByOwner);
    std::vector<double> retained;
    ASSERT_TRUE(reg.subscribe("pos", TopicSubscription{1, 0, 0}, nullptr, &retained));
    EXPECT_TRUE(retained.empty());

    ASSERT_TRUE(reg.publish("pos", {1.0, 2.0, 3.0}, nullptr, nullptr));
    ASSERT_TRUE(reg.subscribe("pos", TopicSubscription{1, 0, 0}, nullptr, &retained));
    EXPECT_EQ(retained, (std::vector<double>{1.0, 2.0, 3.0}));
}

TEST(BugfixDaemonAgent, RedeclareSameKindKeepsRing)
{
    TopicRegistry reg;
    reg.declare("pos", ipc::TopicKind::Scalar2D, "", 16, 1);
    ASSERT_TRUE(reg.publish("pos", {1.0, 2.0}, nullptr, nullptr));
    reg.on_client_disconnect(1);
    reg.declare("pos", ipc::TopicKind::Scalar2D, "", 16, 2);

    std::vector<double> retained;
    ASSERT_TRUE(reg.subscribe("pos", TopicSubscription{1, 0, 0}, nullptr, &retained));
    EXPECT_EQ(retained, (std::vector<double>{1.0, 2.0}));
}

TEST(BugfixDaemonAgent, RingCapacityQuery)
{
    TopicRegistry reg;
    EXPECT_FALSE(reg.ring_capacity_of("x").has_value());
    reg.declare("x", ipc::TopicKind::Scalar2D, "", 0, 1);
    EXPECT_EQ(reg.ring_capacity_of("x").value_or(0), 4096u);
    reg.declare("y", ipc::TopicKind::Scalar2D, "", 32, 1);
    EXPECT_EQ(reg.ring_capacity_of("y").value_or(0), 32u);
}
