#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include <spectra/fwd.hpp>

#include "vk_swapchain.hpp"
#include "../backend.hpp"

namespace spectra
{

// Per-window Vulkan resources.
// Everything that is tied to a specific OS window lives here.
// Shared resources (VkInstance, VkDevice, pipelines, descriptor pool,
// series GPU buffers) remain in VulkanBackend.
struct WindowContext
{
    WindowContext() = default;
    ~WindowContext();

    // Non-copyable (owns unique_ptrs)
    WindowContext(const WindowContext&) = delete;
    WindowContext& operator=(const WindowContext&) = delete;
    WindowContext(WindowContext&&) = default;
    WindowContext& operator=(WindowContext&&) = default;

    // Identity
    uint32_t id = 0;

    // GLFW window handle (nullptr for headless / primary before adoption)
    void* glfw_window = nullptr;

    // Vulkan surface + swapchain (tied to OS window handle)
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vk::SwapchainContext swapchain;

    // Per-window command buffers (indexed by swapchain image or 1 for headless)
    std::vector<VkCommandBuffer> command_buffers;
    VkCommandBuffer current_cmd = VK_NULL_HANDLE;
    uint32_t current_image_index = 0;

    // Per-window sync objects
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    uint32_t current_flight_frame = 0;

    // Per-window frame UBO (different viewport dimensions per window)
    BufferHandle frame_ubo_buffer;

    // Swapchain state
    bool swapchain_dirty = false;
    bool should_close = false;

    // Window state
    bool is_focused = false;

    // Figure assignment: FigureId from FigureRegistry that this window renders.
    // INVALID_FIGURE_ID means "use the primary window's active figure" (default).
    FigureId assigned_figure_index = INVALID_FIGURE_ID;

    // Multi-figure support: ordered list of figures assigned to this window.
    // Used by WindowUIContext for per-window tab management.
    std::vector<FigureId> assigned_figures;
    FigureId active_figure_id = INVALID_FIGURE_ID;
    std::string title;

    // Resize state
    bool needs_resize = false;
    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    std::chrono::steady_clock::time_point resize_time;

    // Per-window ImGui context (nullptr if this window has no ImGui).
    // Owned by this WindowContext — destroyed in VulkanBackend::destroy_window_context().
    // For the primary window, ImGui context is managed by ImGuiIntegration::init/shutdown.
    void* imgui_context = nullptr;

    // Per-window UI subsystem bundle (owned by this window).
    // nullptr for legacy secondary windows that have no ImGui.
    // Set for windows created via WindowManager::create_window_with_ui().
    std::unique_ptr<WindowUIContext> ui_ctx;

};

}  // namespace spectra
