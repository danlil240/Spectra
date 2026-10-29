// test_plugin_concurrent.cpp — two third-party plug-ins loaded at once.
//
// mock_leaky_plugin ("MockLeakyPlugin", transform LeakyShift = +1) and its
// second build mock_leaky_plugin_b ("MockLeakyPluginB", LeakyShiftB = +10)
// each register one extension of every kind.  The tests interleave enable /
// disable / unload / reload of the two while their transforms (retained
// pipeline copies) and custom series types (looked up the way the Renderer
// does every frame) are in use, and check that every step only affects the
// plug-in it names.  A worker thread keeps applying the transform pipeline
// concurrently (DataTransform copies are revoked under a lock, so applying
// them from another thread while the owner is disabled or unloaded is
// supported); series-type use is serialised with the lifecycle steps like the
// render thread (unregistering a series type is a render-thread operation).
//
// Run under ASan (see BUILD_ENVIRONMENT.md §8) to catch use-after-unload.

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <spectra/custom_series.hpp>
#include "adapters/data_source_registry.hpp"
#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "render/series_type_registry.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifndef SPECTRA_MOCK_LEAKY_PLUGIN_PATH
    #define SPECTRA_MOCK_LEAKY_PLUGIN_PATH ""
#endif
#ifndef SPECTRA_MOCK_LEAKY_PLUGIN_B_PATH
    #define SPECTRA_MOCK_LEAKY_PLUGIN_B_PATH ""
#endif

using namespace spectra;

namespace
{

constexpr const char* kA = "MockLeakyPlugin";
constexpr const char* kB = "MockLeakyPluginB";

bool contains(const std::vector<std::string>& values, const std::string& name)
{
    for (const auto& v : values)
    {
        if (v == name)
            return true;
    }
    return false;
}

class PluginConcurrentTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        path_a_ = SPECTRA_MOCK_LEAKY_PLUGIN_PATH;
        path_b_ = SPECTRA_MOCK_LEAKY_PLUGIN_B_PATH;
        if (path_a_.empty() || !std::filesystem::exists(path_a_) || path_b_.empty()
            || !std::filesystem::exists(path_b_))
            GTEST_SKIP() << "mock_leaky_plugin(_b) not found: " << path_a_ << " / " << path_b_;

        mgr_.set_command_registry(&commands_);
        mgr_.set_shortcut_manager(&shortcuts_);
        mgr_.set_undo_manager(&undo_);
        mgr_.set_transform_registry(&transforms_);
        mgr_.set_overlay_registry(&overlays_);
        mgr_.set_export_format_registry(&exports_);
        mgr_.set_data_source_registry(&sources_);
        mgr_.set_series_type_registry(&series_types_);

        // Stand-in for the Renderer's unregister listener: count live types
        // removed while their series still exist.
        listener_ = series_types_.add_unregister_listener(
            [this](SeriesTypeEntry& e) { removed_types_.push_back(e.type_name); });
    }

    void TearDown() override
    {
        stop_worker();
        mgr_.unload_all();
        series_types_.remove_unregister_listener(listener_);
    }

    const void* owner(const char* name) const
    {
        const PluginEntry* p = mgr_.find_plugin(name);
        return p ? p->owner : nullptr;
    }

    // What the Renderer does for a custom series each frame: look the type up
    // by name, skip it while disabled, otherwise call into the plug-in.
    // Returns true when plug-in code ran.
    bool use_series(const CustomSeries& s)
    {
        std::lock_guard frame(frame_mutex_);
        auto            entry = series_types_.find_shared(s.type_name());
        if (!entry || !entry->enabled.load() || !entry->bounds_fn)
            return false;
        float b[4] = {};
        return entry->bounds_fn(s.data(), s.element_count(), b) == 0 && b[1] == 1.0f;
    }

    // y of the retained [LeakyShift, LeakyShiftB] pipeline for input 0.
    float apply_pipeline(const TransformPipeline& p) const
    {
        std::vector<float> xo, yo;
        p.apply(x_, y_, xo, yo);
        return yo.empty() ? -1.0f : yo[0];
    }

    // Applies the retained pipeline on a worker thread until stop_worker();
    // every result must be one of the four valid enable combinations.
    void start_worker(const TransformPipeline& p)
    {
        worker_pipeline_ = p;
        stop_.store(false);
        worker_ = std::thread(
            [this]
            {
                while (!stop_.load())
                {
                    const float v = apply_pipeline(worker_pipeline_);
                    if (v != 0.0f && v != 1.0f && v != 10.0f && v != 11.0f)
                        bad_worker_results_.fetch_add(1);
                    worker_runs_.fetch_add(1);
                }
            });
    }

    void stop_worker()
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // Every lifecycle step: let the worker run a few more applications so it
    // overlaps the step that follows.
    void let_worker_run()
    {
        const uint64_t target = worker_runs_.load() + 50;
        while (worker_.joinable() && worker_runs_.load() < target)
            std::this_thread::yield();
    }

    std::string path_a_, path_b_;

    CommandRegistry      commands_;
    ShortcutManager      shortcuts_;
    UndoManager          undo_;
    TransformRegistry    transforms_;
    OverlayRegistry      overlays_;
    ExportFormatRegistry exports_;
    DataSourceRegistry   sources_;
    SeriesTypeRegistry   series_types_;
    PluginManager        mgr_;   // destroyed (and unloads) before the registries

    uint64_t                 listener_ = 0;
    std::vector<std::string> removed_types_;
    std::mutex               frame_mutex_;

    const std::vector<float> x_{0.0f, 1.0f, 2.0f};
    const std::vector<float> y_{0.0f, 0.0f, 0.0f};

    TransformPipeline     worker_pipeline_;
    std::thread           worker_;
    std::atomic<bool>     stop_{false};
    std::atomic<uint64_t> worker_runs_{0};
    std::atomic<uint32_t> bad_worker_results_{0};
};

}   // namespace

TEST_F(PluginConcurrentTest, BothPluginsCoexistWithoutCollisions)
{
    ASSERT_TRUE(mgr_.load_plugin(path_a_));
    ASSERT_TRUE(mgr_.load_plugin(path_b_));
    ASSERT_EQ(mgr_.plugin_count(), 2u);
    ASSERT_NE(owner(kA), nullptr);
    ASSERT_NE(owner(kB), nullptr);
    EXPECT_NE(owner(kA), owner(kB));

    for (const char* name : {kA, kB})
    {
        SCOPED_TRACE(name);
        const void* o = owner(name);
        EXPECT_EQ(commands_.ids_owned_by(o).size(), 2u);
        EXPECT_EQ(transforms_.count_owned(o), 1u);
        EXPECT_EQ(overlays_.count_owned(o), 1u);
        EXPECT_EQ(exports_.count_owned(o), 1u);
        EXPECT_EQ(sources_.count_owned(o), 1u);
        EXPECT_EQ(series_types_.count_owned(o), 1u);
    }
    EXPECT_EQ(undo_.undo_count(), 2u);
    EXPECT_EQ(series_types_.count(), 2u);
    EXPECT_TRUE(commands_.execute("leaky.ping"));
    EXPECT_TRUE(commands_.execute("leaky.pingB"));
}

TEST_F(PluginConcurrentTest, InterleavedLifecycleWhileSeriesAndTransformsAreInUse)
{
    ASSERT_TRUE(mgr_.load_plugin(path_a_));
    ASSERT_TRUE(mgr_.load_plugin(path_b_));

    // Transforms in use: a retained pipeline of both plug-ins' transforms
    // (also saved as a preset) applied concurrently by the worker thread.
    DataTransform ta, tb;
    ASSERT_TRUE(transforms_.get_transform("LeakyShift", ta));
    ASSERT_TRUE(transforms_.get_transform("LeakyShiftB", tb));
    TransformPipeline pipeline;
    pipeline.push_back(ta);
    pipeline.push_back(tb);
    transforms_.save_pipeline("both", pipeline);
    EXPECT_FLOAT_EQ(apply_pipeline(pipeline), 11.0f);

    // Series in use: one live custom series of each plug-in's type.
    CustomSeries sa("leaky_series"), sb("leaky_seriesB");
    const float  pts[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    sa.set_data(pts, sizeof(pts), 2);
    sb.set_data(pts, sizeof(pts), 2);
    EXPECT_TRUE(use_series(sa));
    EXPECT_TRUE(use_series(sb));

    start_worker(pipeline);
    auto check = [&](float want_pipeline, bool want_a, bool want_b)
    {
        let_worker_run();
        EXPECT_FLOAT_EQ(apply_pipeline(pipeline), want_pipeline);
        EXPECT_EQ(use_series(sa), want_a);
        EXPECT_EQ(use_series(sb), want_b);
        EXPECT_EQ(commands_.execute("leaky.ping"), want_a);
        EXPECT_EQ(commands_.execute("leaky.pingB"), want_b);
    };

    for (int round = 0; round < 3; ++round)
    {
        SCOPED_TRACE(round);
        {
            SCOPED_TRACE("disable A");
            mgr_.set_plugin_enabled(kA, false);
            check(10.0f, false, true);
        }
        {
            SCOPED_TRACE("disable B too");
            mgr_.set_plugin_enabled(kB, false);
            check(0.0f, false, false);
        }
        {
            SCOPED_TRACE("enable A while B is disabled");
            mgr_.set_plugin_enabled(kA, true);
            check(1.0f, true, false);
        }
        {
            SCOPED_TRACE("unload disabled B while A's series and transform run");
            const void* ob = owner(kB);
            {
                std::lock_guard frame(frame_mutex_);
                removed_types_.clear();
                ASSERT_TRUE(mgr_.unload_plugin(kB));
            }
            EXPECT_EQ(removed_types_, std::vector<std::string>{"leaky_seriesB"});
            EXPECT_EQ(mgr_.plugin_count(), 1u);
            EXPECT_EQ(transforms_.count_owned(ob), 0u);
            EXPECT_FALSE(contains(transforms_.available_transforms(), "LeakyShiftB"));
            EXPECT_TRUE(contains(transforms_.available_transforms(), "LeakyShift"));
            EXPECT_EQ(series_types_.count(), 1u);
            EXPECT_EQ(undo_.undo_count(), 1u);   // A's init-time undo action only
            EXPECT_EQ(commands_.find("leaky.pingB"), nullptr);
            EXPECT_NE(commands_.find("leaky.ping"), nullptr);
            EXPECT_EQ(sources_.find("LeakySourceB"), nullptr);
            EXPECT_NE(sources_.find("LeakySource"), nullptr);
            // The retained copy of B's transform is revoked (identity).
            check(1.0f, true, false);
        }
        {
            SCOPED_TRACE("reload B while A is in use");
            {
                std::lock_guard frame(frame_mutex_);
                ASSERT_TRUE(mgr_.load_plugin(path_b_));
            }
            // Live series of B's type rebind by name; the old retained copy
            // stays revoked, the saved preset binds to the new registration.
            check(1.0f, true, true);
            TransformPipeline reloaded;
            ASSERT_TRUE(transforms_.load_pipeline("both", reloaded));
            EXPECT_FLOAT_EQ(apply_pipeline(reloaded), 11.0f);
        }
        {
            SCOPED_TRACE("unload enabled A while B is in use");
            const void* oa = owner(kA);
            {
                std::lock_guard frame(frame_mutex_);
                removed_types_.clear();
                ASSERT_TRUE(mgr_.unload_plugin(kA));
            }
            EXPECT_EQ(removed_types_, std::vector<std::string>{"leaky_series"});
            EXPECT_EQ(transforms_.count_owned(oa), 0u);
            EXPECT_EQ(overlays_.count_owned(oa), 0u);
            EXPECT_EQ(exports_.count_owned(oa), 0u);
            EXPECT_TRUE(contains(transforms_.available_transforms(), "LeakyShiftB"));
            check(0.0f, false, true);
            // The preset names a transform that is gone: it does not load.
            TransformPipeline reloaded;
            EXPECT_FALSE(transforms_.load_pipeline("both", reloaded));
        }
        {
            SCOPED_TRACE("reload A");
            {
                std::lock_guard frame(frame_mutex_);
                ASSERT_TRUE(mgr_.load_plugin(path_a_));
            }
            EXPECT_EQ(mgr_.plugin_count(), 2u);
            check(0.0f, true, true);   // the worker's copies of both are revoked
            TransformPipeline reloaded;
            ASSERT_TRUE(transforms_.load_pipeline("both", reloaded));
            EXPECT_FLOAT_EQ(apply_pipeline(reloaded), 11.0f);
            // Restart the worker on the fresh registrations for the next round.
            stop_worker();
            pipeline = reloaded;
            start_worker(pipeline);
            check(11.0f, true, true);
        }
    }

    stop_worker();
    EXPECT_EQ(bad_worker_results_.load(), 0u);
    EXPECT_GT(worker_runs_.load(), 0u);

    // Unloading both leaves nothing that could call into unmapped code.
    {
        std::lock_guard frame(frame_mutex_);
        mgr_.unload_all();
    }
    EXPECT_EQ(mgr_.plugin_count(), 0u);
    EXPECT_EQ(series_types_.count(), 0u);
    EXPECT_EQ(sources_.count(), 0u);
    EXPECT_EQ(overlays_.count(), 0u);
    EXPECT_EQ(exports_.count(), 0u);
    EXPECT_TRUE(commands_.all_commands().empty());
    EXPECT_FALSE(use_series(sa));
    EXPECT_FALSE(use_series(sb));
    EXPECT_FLOAT_EQ(apply_pipeline(pipeline), 0.0f);
    OverlayDrawContext ctx{};
    overlays_.draw_all(ctx);
}
