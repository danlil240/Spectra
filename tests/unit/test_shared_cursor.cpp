#include <algorithm>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <thread>
#include <vector>

#include "ui/axis_link.hpp"

using namespace spectra;

// ─── Helper ─────────────────────────────────────────────────────────────────

static std::unique_ptr<Figure> make_figure(int n_axes)
{
    auto fig = std::make_unique<Figure>();
    for (int i = 0; i < n_axes; ++i)
    {
        auto& ax = fig->subplot(1, n_axes, i + 1);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.0f, 1.0f);
    }
    return fig;
}

static Axes& ax(Figure& fig, int idx)
{
    return *fig.axes_mut()[static_cast<size_t>(idx)];
}

class SharedCursorTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_ = make_figure(3);
        // Link ax0 and ax1 via X
        group_id_ = mgr_.create_group("shared", LinkAxis::X);
        mgr_.add_to_group(group_id_, &ax(*fig_, 0));
        mgr_.add_to_group(group_id_, &ax(*fig_, 1));
        // ax2 is NOT linked
    }

    std::unique_ptr<Figure> fig_;
    AxisLinkManager         mgr_;
    LinkGroupId             group_id_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// SharedCursor struct
// ═══════════════════════════════════════════════════════════════════════════

TEST(SharedCursorStruct, DefaultInvalid)
{
    SharedCursor sc;
    EXPECT_FALSE(sc.valid);
    EXPECT_FLOAT_EQ(sc.data_x, 0.0f);
    EXPECT_FLOAT_EQ(sc.data_y, 0.0f);
    EXPECT_EQ(sc.source_axes, nullptr);
}

TEST(SharedCursorStruct, SetValues)
{
    SharedCursor sc;
    sc.valid    = true;
    sc.data_x   = 5.0f;
    sc.data_y   = -0.5f;
    sc.screen_x = 100.0;
    sc.screen_y = 200.0;

    EXPECT_TRUE(sc.valid);
    EXPECT_FLOAT_EQ(sc.data_x, 5.0f);
    EXPECT_FLOAT_EQ(sc.data_y, -0.5f);
    EXPECT_DOUBLE_EQ(sc.screen_x, 100.0);
    EXPECT_DOUBLE_EQ(sc.screen_y, 200.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Update and query
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(SharedCursorTest, UpdateAndQueryLinkedAxes)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.data_y      = 0.3f;
    sc.screen_x    = 150.0;
    sc.screen_y    = 250.0;
    sc.source_axes = &ax(*fig_, 0);

    mgr_.update_shared_cursor(sc);

    // ax1 is linked to ax0 — should see the cursor
    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.data_x, 5.0f);
    EXPECT_FLOAT_EQ(result.data_y, 0.3f);
    EXPECT_DOUBLE_EQ(result.screen_x, 150.0);
    EXPECT_EQ(result.source_axes, &ax(*fig_, 0));
}

TEST_F(SharedCursorTest, SourceAxesSeesOwnCursor)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 3.0f;
    sc.source_axes = &ax(*fig_, 0);

    mgr_.update_shared_cursor(sc);

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 0));
    EXPECT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.data_x, 3.0f);
}

TEST_F(SharedCursorTest, UnlinkedAxesDoesNotSeeCursor)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax(*fig_, 0);

    mgr_.update_shared_cursor(sc);

    // ax2 is NOT in the group
    auto result = mgr_.shared_cursor_for(&ax(*fig_, 2));
    EXPECT_FALSE(result.valid);
}

TEST_F(SharedCursorTest, ClearCursor)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc);

    mgr_.clear_shared_cursor();

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_FALSE(result.valid);
}

TEST_F(SharedCursorTest, InvalidCursorNotBroadcast)
{
    SharedCursor sc;
    sc.valid       = false;
    sc.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc);

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_FALSE(result.valid);
}

TEST_F(SharedCursorTest, NullSourceNotBroadcast)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = nullptr;
    mgr_.update_shared_cursor(sc);

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_FALSE(result.valid);
}

TEST_F(SharedCursorTest, NullQueryReturnsInvalid)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc);

    auto result = mgr_.shared_cursor_for(nullptr);
    EXPECT_FALSE(result.valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// Multiple groups
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(SharedCursorTest, CursorAcrossMultipleGroups)
{
    // Create a second group linking ax1 and ax2
    auto g2 = mgr_.create_group("group2", LinkAxis::Y);
    mgr_.add_to_group(g2, &ax(*fig_, 1));
    mgr_.add_to_group(g2, &ax(*fig_, 2));

    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 7.0f;
    sc.source_axes = &ax(*fig_, 1);
    mgr_.update_shared_cursor(sc);

    // ax0 is in group1 with ax1 — should see cursor
    auto r0 = mgr_.shared_cursor_for(&ax(*fig_, 0));
    EXPECT_TRUE(r0.valid);

    // ax2 is in group2 with ax1 — should also see cursor
    auto r2 = mgr_.shared_cursor_for(&ax(*fig_, 2));
    EXPECT_TRUE(r2.valid);
}

TEST_F(SharedCursorTest, CursorFromUnlinkedSource)
{
    // ax2 is not linked to anything
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax(*fig_, 2);
    mgr_.update_shared_cursor(sc);

    // ax0 should NOT see cursor from ax2
    auto result = mgr_.shared_cursor_for(&ax(*fig_, 0));
    EXPECT_FALSE(result.valid);

    // ax2 should see its own cursor
    auto self = mgr_.shared_cursor_for(&ax(*fig_, 2));
    EXPECT_TRUE(self.valid);
}

// ═══════════════════════════════════════════════════════════════════════════
// Cursor updates overwrite previous
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(SharedCursorTest, LatestCursorWins)
{
    SharedCursor sc1;
    sc1.valid       = true;
    sc1.data_x      = 3.0f;
    sc1.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc1);

    SharedCursor sc2;
    sc2.valid       = true;
    sc2.data_x      = 7.0f;
    sc2.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc2);

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.data_x, 7.0f);
}

TEST_F(SharedCursorTest, DifferentSourceOverwrites)
{
    SharedCursor sc1;
    sc1.valid       = true;
    sc1.data_x      = 3.0f;
    sc1.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc1);

    // Now ax1 becomes the source
    SharedCursor sc2;
    sc2.valid       = true;
    sc2.data_x      = 8.0f;
    sc2.source_axes = &ax(*fig_, 1);
    mgr_.update_shared_cursor(sc2);

    auto result = mgr_.shared_cursor_for(&ax(*fig_, 0));
    EXPECT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.data_x, 8.0f);
    EXPECT_EQ(result.source_axes, &ax(*fig_, 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// Thread safety
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(SharedCursorTest, ConcurrentUpdateAndQuery)
{
    constexpr int            N = 200;
    std::vector<std::thread> threads;

    // Writer thread: rapidly update cursor
    threads.emplace_back(
        [&]()
        {
            for (int i = 0; i < N; ++i)
            {
                SharedCursor sc;
                sc.valid       = true;
                sc.data_x      = static_cast<float>(i);
                sc.source_axes = &ax(*fig_, 0);
                mgr_.update_shared_cursor(sc);
            }
        });

    // Reader thread: rapidly query cursor
    threads.emplace_back(
        [&]()
        {
            for (int i = 0; i < N; ++i)
            {
                auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
                // Just ensure no crash; result may or may not be valid
                (void)result;
            }
        });

    // Clear thread
    threads.emplace_back(
        [&]()
        {
            for (int i = 0; i < N; ++i)
            {
                if (i % 10 == 0)
                    mgr_.clear_shared_cursor();
            }
        });

    for (auto& t : threads)
        t.join();
    // No crash = pass
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration with link/unlink
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(SharedCursorTest, UnlinkStopsCursorSharing)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc);

    // Verify ax1 sees cursor
    EXPECT_TRUE(mgr_.shared_cursor_for(&ax(*fig_, 1)).valid);

    // Unlink ax1
    mgr_.remove_from_group(group_id_, &ax(*fig_, 1));

    // ax1 should no longer see cursor
    auto result = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_FALSE(result.valid);
}

TEST_F(SharedCursorTest, RemoveGroupStopsCursorSharing)
{
    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax(*fig_, 0);
    mgr_.update_shared_cursor(sc);

    mgr_.remove_group(group_id_);

    // Neither ax0 nor ax1 should see cursor via group
    // (ax0 still sees its own cursor since it's the source)
    auto r0 = mgr_.shared_cursor_for(&ax(*fig_, 0));
    EXPECT_TRUE(r0.valid);   // Source always sees own

    auto r1 = mgr_.shared_cursor_for(&ax(*fig_, 1));
    EXPECT_FALSE(r1.valid);   // No longer linked
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(SharedCursorEdge, NoGroupsAtAll)
{
    AxisLinkManager mgr;
    Axes            ax;

    SharedCursor sc;
    sc.valid       = true;
    sc.data_x      = 5.0f;
    sc.source_axes = &ax;
    mgr.update_shared_cursor(sc);

    // Source sees own cursor
    auto result = mgr.shared_cursor_for(&ax);
    EXPECT_TRUE(result.valid);
}

TEST(SharedCursorEdge, EmptyGroup)
{
    AxisLinkManager mgr;
    mgr.create_group("empty", LinkAxis::X);

    Axes         ax;
    SharedCursor sc;
    sc.valid       = true;
    sc.source_axes = &ax;
    mgr.update_shared_cursor(sc);

    auto result = mgr.shared_cursor_for(&ax);
    EXPECT_TRUE(result.valid);   // Source sees own
}

TEST(SharedCursorEdge, ClearThenQuery)
{
    AxisLinkManager mgr;
    mgr.clear_shared_cursor();

    Axes ax;
    auto result = mgr.shared_cursor_for(&ax);
    EXPECT_FALSE(result.valid);
}

TEST(SharedCursorEdge, RapidUpdateClear)
{
    AxisLinkManager mgr;
    auto            fig = make_figure(2);
    auto            gid = mgr.create_group("g", LinkAxis::X);
    mgr.add_to_group(gid, &ax(*fig, 0));
    mgr.add_to_group(gid, &ax(*fig, 1));

    for (int i = 0; i < 100; ++i)
    {
        SharedCursor sc;
        sc.valid       = true;
        sc.data_x      = static_cast<float>(i);
        sc.source_axes = &ax(*fig, 0);
        mgr.update_shared_cursor(sc);
        mgr.clear_shared_cursor();
    }

    // After clear, no cursor visible
    auto result = mgr.shared_cursor_for(&ax(*fig, 1));
    EXPECT_FALSE(result.valid);
}
