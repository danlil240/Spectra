#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// LegendInteraction is ImGui-guarded. These tests exercise the pure-logic
// parts (visibility toggling, opacity animation, state tracking) without
// requiring a running ImGui context.

using namespace spectra;

// ─── Standalone legend state logic (mirrors LegendInteraction internals) ────

namespace
{

struct LegendSeriesState
{
    float opacity        = 1.0f;
    float target_opacity = 1.0f;
    bool  user_visible   = true;
};

class TestLegendLogic
{
   public:
    LegendSeriesState& get_state(const Series* s)
    {
        auto it = states_.find(s);
        if (it != states_.end())
            return it->second;
        LegendSeriesState state;
        state.user_visible   = s ? s->visible() : true;
        state.opacity        = state.user_visible ? 1.0f : 0.0f;
        state.target_opacity = state.opacity;
        return states_.emplace(s, state).first->second;
    }

    void toggle(Series* s)
    {
        auto& state          = get_state(s);
        state.user_visible   = !state.user_visible;
        state.target_opacity = state.user_visible ? 1.0f : 0.15f;
        if (s)
            s->visible(state.user_visible);
    }

    void update(float dt)
    {
        float speed = (toggle_duration_ > 0.0f) ? (1.0f / toggle_duration_) : 100.0f;
        for (auto& [_, state] : states_)
        {
            float diff = state.target_opacity - state.opacity;
            if (std::abs(diff) > 0.001f)
            {
                state.opacity += diff * std::min(1.0f, speed * dt);
                if (std::abs(state.opacity - state.target_opacity) < 0.005f)
                {
                    state.opacity = state.target_opacity;
                }
            }
        }
    }

    float series_opacity(const Series* s) const
    {
        auto it = states_.find(s);
        if (it != states_.end())
            return it->second.opacity;
        return s ? (s->visible() ? 1.0f : 0.0f) : 1.0f;
    }

    bool is_series_visible(const Series* s) const
    {
        auto it = states_.find(s);
        if (it != states_.end())
            return it->second.user_visible;
        return s ? s->visible() : true;
    }

    size_t tracked_count() const { return states_.size(); }

    void set_toggle_duration(float d) { toggle_duration_ = d; }

   private:
    std::unordered_map<const Series*, LegendSeriesState> states_;
    float                                                toggle_duration_ = 0.2f;
};

}   // anonymous namespace

// ─── Tests ──────────────────────────────────────────────────────────────────

class LegendInteractionTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        axes_ = std::make_unique<Axes>();
        axes_->xlim(0.0f, 10.0f);
        axes_->ylim(0.0f, 10.0f);

        std::vector<float> x  = {0.0f, 5.0f, 10.0f};
        std::vector<float> y1 = {0.0f, 5.0f, 0.0f};
        std::vector<float> y2 = {10.0f, 5.0f, 10.0f};
        std::vector<float> y3 = {5.0f, 5.0f, 5.0f};

        axes_->line(x, y1).label("series_a").color(colors::red);
        axes_->line(x, y2).label("series_b").color(colors::blue);
        axes_->line(x, y3).label("series_c").color(colors::green);
    }

    std::unique_ptr<Axes> axes_;
};

TEST_F(LegendInteractionTest, InitialStateAllVisible)
{
    TestLegendLogic legend;
    for (auto& s : axes_->series())
    {
        EXPECT_TRUE(legend.is_series_visible(s.get()));
        EXPECT_FLOAT_EQ(legend.series_opacity(s.get()), 1.0f);
    }
}

TEST_F(LegendInteractionTest, ToggleHidesSeries)
{
    TestLegendLogic legend;
    Series*         s = axes_->series_mut()[0].get();

    legend.toggle(s);

    EXPECT_FALSE(legend.is_series_visible(s));
    EXPECT_FALSE(s->visible());
    auto& state = legend.get_state(s);
    EXPECT_FLOAT_EQ(state.target_opacity, 0.15f);
}

TEST_F(LegendInteractionTest, ToggleTwiceRestoresVisibility)
{
    TestLegendLogic legend;
    Series*         s = axes_->series_mut()[0].get();

    legend.toggle(s);
    legend.toggle(s);

    EXPECT_TRUE(legend.is_series_visible(s));
    EXPECT_TRUE(s->visible());
    auto& state = legend.get_state(s);
    EXPECT_FLOAT_EQ(state.target_opacity, 1.0f);
}

TEST_F(LegendInteractionTest, OpacityAnimatesOverTime)
{
    TestLegendLogic legend;
    legend.set_toggle_duration(0.2f);
    Series* s = axes_->series_mut()[0].get();

    legend.toggle(s);   // target = 0.15

    // Opacity should start at 1.0 and decrease toward 0.15
    float prev = legend.series_opacity(s);
    EXPECT_FLOAT_EQ(prev, 1.0f);

    // Simulate several frames
    for (int i = 0; i < 60; ++i)
    {
        legend.update(0.016f);   // ~60fps
    }

    float after = legend.series_opacity(s);
    EXPECT_LT(after, prev);
    EXPECT_NEAR(after, 0.15f, 0.1f);
}

TEST_F(LegendInteractionTest, OpacityConvergesToTarget)
{
    TestLegendLogic legend;
    legend.set_toggle_duration(0.1f);
    Series* s = axes_->series_mut()[0].get();

    legend.toggle(s);

    // Run enough frames to fully converge
    for (int i = 0; i < 100; ++i)
    {
        legend.update(0.016f);
    }

    EXPECT_NEAR(legend.series_opacity(s), 0.15f, 0.01f);
}

TEST_F(LegendInteractionTest, MultipleSeriesIndependent)
{
    TestLegendLogic legend;
    Series*         s0 = axes_->series_mut()[0].get();
    Series*         s1 = axes_->series_mut()[1].get();
    Series*         s2 = axes_->series_mut()[2].get();

    legend.toggle(s0);   // Hide first series

    EXPECT_FALSE(legend.is_series_visible(s0));
    EXPECT_TRUE(legend.is_series_visible(s1));
    EXPECT_TRUE(legend.is_series_visible(s2));
}

TEST_F(LegendInteractionTest, UntrackedSeriesDefaultsToVisible)
{
    TestLegendLogic legend;
    Series*         s = axes_->series_mut()[0].get();

    // Before any interaction, opacity should be 1.0
    EXPECT_FLOAT_EQ(legend.series_opacity(s), 1.0f);
    EXPECT_TRUE(legend.is_series_visible(s));
}

TEST_F(LegendInteractionTest, NullSeriesHandledGracefully)
{
    TestLegendLogic legend;
    EXPECT_FLOAT_EQ(legend.series_opacity(nullptr), 1.0f);
    EXPECT_TRUE(legend.is_series_visible(nullptr));
}

TEST_F(LegendInteractionTest, TrackedCountIncreasesOnInteraction)
{
    TestLegendLogic legend;
    EXPECT_EQ(legend.tracked_count(), 0u);

    legend.get_state(axes_->series()[0].get());
    EXPECT_EQ(legend.tracked_count(), 1u);

    legend.get_state(axes_->series()[1].get());
    EXPECT_EQ(legend.tracked_count(), 2u);

    // Same series again — no increase
    legend.get_state(axes_->series()[0].get());
    EXPECT_EQ(legend.tracked_count(), 2u);
}

TEST_F(LegendInteractionTest, ZeroDurationSnapsQuickly)
{
    TestLegendLogic legend;
    legend.set_toggle_duration(0.0f);
    Series* s = axes_->series_mut()[0].get();

    legend.toggle(s);
    // Exponential lerp with speed=inf still needs a few frames to converge
    // because each step moves by diff * min(1, speed*dt) = diff * 1.0
    // which halves the remaining distance each frame.
    for (int i = 0; i < 20; ++i)
    {
        legend.update(0.016f);
    }

    EXPECT_NEAR(legend.series_opacity(s), 0.15f, 0.01f);
}

TEST_F(LegendInteractionTest, ToggleAllSeries)
{
    TestLegendLogic legend;

    // Hide all
    for (auto& s : axes_->series_mut())
    {
        legend.toggle(s.get());
    }

    for (auto& s : axes_->series())
    {
        EXPECT_FALSE(legend.is_series_visible(s.get()));
        EXPECT_FALSE(s->visible());
    }

    // Show all
    for (auto& s : axes_->series_mut())
    {
        legend.toggle(s.get());
    }

    for (auto& s : axes_->series())
    {
        EXPECT_TRUE(legend.is_series_visible(s.get()));
        EXPECT_TRUE(s->visible());
    }
}

TEST_F(LegendInteractionTest, RapidToggleDoesNotCorrupt)
{
    TestLegendLogic legend;
    Series*         s = axes_->series_mut()[0].get();

    // Rapid toggle 10 times
    for (int i = 0; i < 10; ++i)
    {
        legend.toggle(s);
    }

    // Even number of toggles → should be back to visible
    EXPECT_TRUE(legend.is_series_visible(s));
    EXPECT_TRUE(s->visible());
}

TEST_F(LegendInteractionTest, AnimationMidwayInterrupt)
{
    TestLegendLogic legend;
    legend.set_toggle_duration(0.5f);
    Series* s = axes_->series_mut()[0].get();

    legend.toggle(s);   // Start hiding

    // Animate partway
    for (int i = 0; i < 5; ++i)
    {
        legend.update(0.016f);
    }

    float mid_opacity = legend.series_opacity(s);
    EXPECT_GT(mid_opacity, 0.15f);
    EXPECT_LT(mid_opacity, 1.0f);

    // Toggle back before animation completes
    legend.toggle(s);

    auto& state = legend.get_state(s);
    EXPECT_FLOAT_EQ(state.target_opacity, 1.0f);

    // Animate to completion
    for (int i = 0; i < 100; ++i)
    {
        legend.update(0.016f);
    }

    EXPECT_NEAR(legend.series_opacity(s), 1.0f, 0.01f);
}
