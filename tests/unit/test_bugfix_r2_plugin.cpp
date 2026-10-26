// test_bugfix_r2_plugin.cpp — Regression tests for plugin unload (bug #108,
// round 2): before a custom series type is unregistered, the Renderer must run
// the plugin's cleanup_fn for every series of that type (live or pending
// deferred deletion) while the plugin is still loaded, and must never hand one
// plugin's per-series state to another plugin re-registering the same name.

#include <gtest/gtest.h>
#include <memory>
#include <spectra/custom_series.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{

// Minimal CPU-only backend.
class FakeBackend : public Backend
{
   public:
    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t, uint32_t) override { return true; }
    bool recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }

    PipelineHandle create_pipeline(PipelineType) override { return PipelineHandle{next_id_++}; }

    BufferHandle create_buffer(BufferUsage, size_t size_bytes) override
    {
        BufferHandle h{next_id_++};
        buffers_[h.id] = size_bytes;
        return h;
    }
    void destroy_buffer(BufferHandle handle) override { buffers_.erase(handle.id); }
    void upload_buffer(BufferHandle, const void*, size_t, size_t) override {}

    TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{next_id_++};
    }
    void destroy_texture(TextureHandle) override {}

    bool     begin_frame(FrameProfiler*) override { return true; }
    void     end_frame(FrameProfiler*) override {}
    void     begin_render_pass(const Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(PipelineHandle) override {}
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }

    uint64_t                             next_id_ = 1;
    std::unordered_map<uint64_t, size_t> buffers_;
};

// Per-series state allocated by a fake plugin; tagged with the plugin id so a
// cleanup can detect state that belongs to another plugin.
struct FakeState
{
    int plugin_id = 0;
};

// Records what a fake plugin's callbacks saw.
struct FakePlugin
{
    int                 id  = 0;
    SeriesTypeRegistry* reg = nullptr;
    std::string         type_name;
    int                 uploads             = 0;
    int                 uploads_with_null   = 0;
    int                 cleanups            = 0;
    int                 foreign_cleanups    = 0;   // cleanup received another plugin's state
    int                 foreign_uploads     = 0;   // upload received another plugin's state
    bool                registered_at_clean = true;

    void register_type()
    {
        reg->register_type(
            type_name,
            CustomPipelineDesc{},
            [this](Backend&, const void*, void*& state, size_t) -> int
            {
                ++uploads;
                if (!state)
                {
                    ++uploads_with_null;
                    state = new FakeState{id};
                }
                else if (static_cast<FakeState*>(state)->plugin_id != id)
                {
                    ++foreign_uploads;
                }
                return 0;
            },
            [](Backend&, PipelineHandle, const void*, const float*, const SeriesPushConstants&)
                -> int { return 0; },
            [](const void*, size_t, float*) -> int { return 0; },
            [this](Backend&, void*& state)
            {
                ++cleanups;
                if (reg->find(type_name) == nullptr)
                    registered_at_clean = false;
                auto* s = static_cast<FakeState*>(state);
                if (s && s->plugin_id != id)
                    ++foreign_cleanups;
                delete s;
                state = nullptr;
            });
    }
};

void set_blob(CustomSeries& s)
{
    const float data[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    s.set_data(data, sizeof(data), 2);
}

}   // namespace

// ─── SeriesTypeRegistry listeners / identity ────────────────────────────────

TEST(BugfixR2Plugin, ListenerRunsBeforeRemovalWithEntryStillRegistered)
{
    SeriesTypeRegistry reg;
    reg.register_type("t", CustomPipelineDesc{}, {}, {}, {});

    int  calls         = 0;
    bool found_in_call = false;
    auto id            = reg.add_unregister_listener(
        [&](SeriesTypeEntry& e)
        {
            ++calls;
            EXPECT_EQ(e.type_name, "t");
            found_in_call = reg.find("t") == &e;
        });

    reg.unregister_type("t");
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(found_in_call);
    EXPECT_EQ(reg.find("t"), nullptr);

    // Unknown names do not notify.
    reg.unregister_type("t");
    EXPECT_EQ(calls, 1);

    reg.remove_unregister_listener(id);
    reg.register_type("t", CustomPipelineDesc{}, {}, {}, {});
    reg.unregister_type("t");
    EXPECT_EQ(calls, 1);
}

TEST(BugfixR2Plugin, FindPointerStableAcrossRegistrations)
{
    SeriesTypeRegistry reg;
    reg.register_type("first", CustomPipelineDesc{}, {}, {}, {});
    const auto* p = reg.find("first");
    ASSERT_NE(p, nullptr);
    for (int i = 0; i < 32; ++i)
        reg.register_type("other_" + std::to_string(i), CustomPipelineDesc{}, {}, {}, {});
    EXPECT_EQ(reg.find("first"), p);
}

TEST(BugfixR2Plugin, UnregisterEntryLeavesSameNamedReplacementAlone)
{
    SeriesTypeRegistry reg;
    reg.register_type("dup", CustomPipelineDesc{}, {}, {}, {});
    std::weak_ptr<SeriesTypeEntry> old_entry = reg.find_shared("dup");

    // Plugin A unregisters its own type; plugin B registers the same name.
    reg.unregister_type("dup");
    reg.register_type("dup", CustomPipelineDesc{}, {}, {}, {});
    auto new_entry = reg.find_shared("dup");
    ASSERT_NE(new_entry, nullptr);

    // Unloading A must not remove B's entry.
    EXPECT_TRUE(old_entry.expired());
    EXPECT_FALSE(reg.unregister_entry(old_entry.lock()));
    EXPECT_EQ(reg.find_shared("dup"), new_entry);

    EXPECT_TRUE(reg.unregister_entry(new_entry));
    EXPECT_EQ(reg.find("dup"), nullptr);
    EXPECT_FALSE(reg.unregister_entry(new_entry));
}

// ─── Renderer releases plugin state on unregister ───────────────────────────

TEST(BugfixR2Plugin, UnregisterRunsCleanupForLiveSeriesWhileRegistered)
{
    SeriesTypeRegistry reg;   // must outlive the renderer
    FakeBackend        backend;
    ui::ThemeManager   theme;
    FakePlugin         a{1, &reg, "blob"};
    a.register_type();

    Renderer renderer(backend, theme);
    renderer.set_series_type_registry(&reg);

    CustomSeries s1("blob");
    CustomSeries s2("blob");
    set_blob(s1);
    set_blob(s2);
    renderer.upload_series_data(s1);
    renderer.upload_series_data(s2);
    ASSERT_EQ(a.uploads_with_null, 2);

    reg.unregister_type("blob");
    EXPECT_EQ(a.cleanups, 2);
    EXPECT_TRUE(a.registered_at_clean);

    // Plugin B re-registers the same name: it starts from a null slot and
    // never sees A's state.
    FakePlugin b{2, &reg, "blob"};
    b.register_type();
    renderer.upload_series_data(s1);
    EXPECT_EQ(b.uploads, 1);
    EXPECT_EQ(b.uploads_with_null, 1);
    EXPECT_EQ(b.foreign_uploads, 0);

    // Detaching releases B's live state through B's cleanup.
    renderer.set_series_type_registry(nullptr);
    EXPECT_EQ(b.cleanups, 1);
    EXPECT_EQ(b.foreign_cleanups, 0);
    EXPECT_EQ(a.cleanups, 2);
}

TEST(BugfixR2Plugin, UnregisterRunsCleanupForSeriesPendingDeletion)
{
    SeriesTypeRegistry reg;
    FakeBackend        backend;
    ui::ThemeManager   theme;
    FakePlugin         a{1, &reg, "blob"};
    a.register_type();

    Renderer renderer(backend, theme);
    renderer.set_series_type_registry(&reg);

    {
        CustomSeries s("blob");
        set_blob(s);
        renderer.upload_series_data(s);
        renderer.notify_series_removed(&s);   // state now waits in the deletion ring
    }
    EXPECT_EQ(a.cleanups, 0);

    reg.unregister_type("blob");
    EXPECT_EQ(a.cleanups, 1);
    EXPECT_TRUE(a.registered_at_clean);

    // A same-named type from another plugin must not get A's state when the
    // deletion ring is flushed later.
    FakePlugin b{2, &reg, "blob"};
    b.register_type();
    for (int i = 0; i < 8; ++i)
        renderer.flush_pending_deletions();
    EXPECT_EQ(b.cleanups, 0);
    EXPECT_EQ(b.foreign_cleanups, 0);
    EXPECT_EQ(a.cleanups, 1);

    renderer.set_series_type_registry(nullptr);
}

TEST(BugfixR2Plugin, ReleaseByNameOnlyAffectsThatType)
{
    SeriesTypeRegistry reg;
    FakeBackend        backend;
    ui::ThemeManager   theme;
    FakePlugin         a{1, &reg, "type_a"};
    FakePlugin         b{2, &reg, "type_b"};
    a.register_type();
    b.register_type();

    Renderer renderer(backend, theme);
    renderer.set_series_type_registry(&reg);

    CustomSeries sa("type_a");
    CustomSeries sb("type_b");
    set_blob(sa);
    set_blob(sb);
    renderer.upload_series_data(sa);
    renderer.upload_series_data(sb);

    renderer.release_custom_type_state("type_a");
    EXPECT_EQ(a.cleanups, 1);
    EXPECT_EQ(b.cleanups, 0);

    // Re-upload rebuilds A's state from a null slot.
    renderer.upload_series_data(sa);
    EXPECT_EQ(a.uploads_with_null, 2);

    renderer.set_series_type_registry(nullptr);
    EXPECT_EQ(a.cleanups, 2);
    EXPECT_EQ(b.cleanups, 1);
}
