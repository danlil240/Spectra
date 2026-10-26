// Regression tests for round-2 "app loops" fixes.
//
// SplitViewManager::deserialize must enforce MAX_PANES the same way
// split_pane() does, and must leave the manager untouched when it rejects
// a layout.

#include <gtest/gtest.h>
#include <string>

#include "render/backend.hpp"
#include "ui/docking/split_view.hpp"

using namespace spectra;

namespace
{

std::string leaf_json(FigureId fig)
{
    return "{\"id\":1,\"leaf\":true,\"figure\":" + std::to_string(fig) + "}";
}

// Right-leaning chain of internal nodes with `leaves` leaf panes.
std::string chain_json(size_t leaves, FigureId first_fig = 1)
{
    if (leaves <= 1)
        return leaf_json(first_fig);
    return "{\"id\":2,\"leaf\":false,\"dir\":\"h\",\"ratio\":0.5,\"first\":" + leaf_json(first_fig)
           + ",\"second\":" + chain_json(leaves - 1, first_fig + 1) + "}";
}

std::string manager_json(FigureId active, size_t leaves)
{
    return "{\"active\":" + std::to_string(active) + ",\"root\":" + chain_json(leaves) + "}";
}

}   // namespace

TEST(BugfixR2SplitView, DeserializeAcceptsMaxPanes)
{
    SplitViewManager mgr;
    mgr.update_layout(Rect{0, 0, 1920, 1080});

    ASSERT_TRUE(mgr.deserialize(manager_json(3, SplitViewManager::MAX_PANES)));
    EXPECT_EQ(mgr.pane_count(), SplitViewManager::MAX_PANES);
    EXPECT_EQ(mgr.active_figure_index(), 3u);
}

TEST(BugfixR2SplitView, DeserializeRejectsMoreThanMaxPanes)
{
    SplitViewManager mgr;
    mgr.update_layout(Rect{0, 0, 1920, 1080});
    const size_t   before_count  = mgr.pane_count();
    const FigureId before_active = mgr.active_figure_index();

    EXPECT_FALSE(mgr.deserialize(manager_json(5, SplitViewManager::MAX_PANES + 1)));

    // Rejected layout leaves the manager unchanged (root and active pane).
    EXPECT_EQ(mgr.pane_count(), before_count);
    EXPECT_EQ(mgr.active_figure_index(), before_active);

    // And the pane limit still holds afterwards.
    EXPECT_LE(mgr.pane_count(), SplitViewManager::MAX_PANES);
}

TEST(BugfixR2SplitView, RoundTripAtLimitThenSplitFails)
{
    SplitViewManager src;
    src.update_layout(Rect{0, 0, 1920, 1080});
    FigureId next = 1;
    while (src.pane_count() < SplitViewManager::MAX_PANES)
    {
        ASSERT_NE(src.split_pane(next - 1, SplitDirection::Horizontal, next, 0.5f), nullptr);
        ++next;
    }

    SplitViewManager dst;
    dst.update_layout(Rect{0, 0, 1920, 1080});
    ASSERT_TRUE(dst.deserialize(src.serialize()));
    EXPECT_EQ(dst.pane_count(), SplitViewManager::MAX_PANES);

    // A deserialized layout at the limit cannot be split further.
    EXPECT_EQ(dst.split_pane(0, SplitDirection::Vertical, next, 0.5f), nullptr);
}

// Backend::is_device_lost() defaults to false so non-Vulkan backends never
// trigger the device-lost shutdown path in the app loops.
namespace
{
class DeviceLostProbeBackend : public Backend
{
   public:
    bool           init(bool) override { return true; }
    void           shutdown() override {}
    void           wait_idle() override {}
    bool           create_surface(void*) override { return true; }
    bool           create_swapchain(uint32_t, uint32_t) override { return true; }
    bool           recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool           create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }
    PipelineHandle create_pipeline(PipelineType) override { return PipelineHandle{1}; }
    BufferHandle   create_buffer(BufferUsage, size_t) override { return BufferHandle{1}; }
    void           destroy_buffer(BufferHandle) override {}
    void           upload_buffer(BufferHandle, const void*, size_t, size_t) override {}
    TextureHandle  create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{1};
    }
    void     destroy_texture(TextureHandle) override {}
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
};
}   // namespace

TEST(BugfixR2DeviceLost, BackendDefaultIsNotLost)
{
    DeviceLostProbeBackend backend;
    const Backend&         base = backend;
    EXPECT_FALSE(base.is_device_lost());
}
