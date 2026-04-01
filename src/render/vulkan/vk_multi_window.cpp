// vk_multi_window.cpp — Multi-window context init, destroy, swapchain recreation.
// Split from vk_backend.cpp (MR-2) for focused module ownership.

#include "vk_backend.hpp"

#include <spectra/logger.hpp>

#include <stdexcept>

#include "platform/window_system/surface_host.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_impl_glfw.h>
    #include <imgui_impl_vulkan.h>
#endif

namespace spectra
{

bool VulkanBackend::recreate_swapchain_for(WindowContext& wctx, uint32_t width, uint32_t height)
{
    auto* prev_active = active_window_;
    active_window_    = &wctx;
    bool ok           = recreate_swapchain(width, height);
    active_window_    = prev_active;
    return ok;
}

bool VulkanBackend::recreate_swapchain_for_with_imgui(WindowContext& wctx,
                                                      uint32_t       width,
                                                      uint32_t       height)
{
    // Fall back to plain recreate if this window has no ImGui context
    if (!wctx.imgui_context)
    {
        return recreate_swapchain_for(wctx, width, height);
    }

    // Recreate the swapchain (saves/restores active_window_ internally)
    if (!recreate_swapchain_for(wctx, width, height))
    {
        return false;
    }

#if defined(SPECTRA_USE_IMGUI) && defined(SPECTRA_USE_GLFW)
    // Section 3F constraint 4: Update only this window's ImGui backend
    // with the new image count.  The render pass handle is reused during
    // recreate_swapchain (format doesn't change on resize), so we only
    // need to update MinImageCount.
    auto* prev_imgui_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx.imgui_context));

    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(wctx.swapchain.images.size()));

    SPECTRA_LOG_INFO("vulkan",
                     "Window " + std::to_string(wctx.id)
                         + " swapchain recreated with ImGui update: "
                         + std::to_string(wctx.swapchain.extent.width) + "x"
                         + std::to_string(wctx.swapchain.extent.height));

    ImGui::SetCurrentContext(prev_imgui_ctx);
#endif

    return true;
}

bool VulkanBackend::init_window_context(WindowContext& wctx, uint32_t width, uint32_t height)
{
    if (!wctx.native_window)
    {
        SPECTRA_LOG_ERROR("vulkan", "init_window_context: no native window set");
        return false;
    }
    if (!surface_host_)
    {
        SPECTRA_LOG_ERROR("vulkan", "init_window_context: no surface host configured");
        return false;
    }

    // Save and restore active window so callers don't need to worry about it
    auto* prev_active = active_window_;

    try
    {
        if (!surface_host_->create_surface(ctx_.instance, wctx.native_window, wctx.surface))
        {
            SPECTRA_LOG_ERROR("vulkan",
                              "init_window_context: surface creation failed for host "
                                  + std::string(surface_host_->name()));
            active_window_ = prev_active;
            return false;
        }
        surface_host_->on_surface_created(wctx.native_window, wctx.surface);

        // Create swapchain for this window
        auto vk_msaa   = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        wctx.swapchain = vk::create_swapchain(
            ctx_.device,
            ctx_.physical_device,
            wctx.surface,
            width,
            height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            vk_msaa);

        // Allocate command buffers and sync objects for this window
        create_command_buffers_for(wctx);
        create_sync_objects_for(wctx);

        active_window_ = prev_active;

        SPECTRA_LOG_INFO("vulkan",
                         "Window context " + std::to_string(wctx.id)
                             + " initialized: " + std::to_string(wctx.swapchain.extent.width) + "x"
                             + std::to_string(wctx.swapchain.extent.height));
        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "init_window_context failed: " + std::string(e.what()));
        destroy_surface_for(wctx);
        active_window_ = prev_active;
        return false;
    }
}

bool VulkanBackend::init_window_context_with_imgui(WindowContext& wctx,
                                                   uint32_t       width,
                                                   uint32_t       height)
{
    // Step 1: Create Vulkan resources (surface, swapchain, cmd buffers, sync)
    if (!init_window_context(wctx, width, height))
    {
        return false;
    }

    // Step 2 (Section 3F constraint 2): Assert swapchain format matches primary.
    // Different surfaces can yield different VkSurfaceFormatKHR on exotic
    // multi-monitor setups.  If they differ, log a warning — the render pass
    // and pipelines were created for the primary's format, so a mismatch would
    // cause validation errors.  In practice this is extremely rare on the same GPU.
    if (wctx.swapchain.image_format != initial_window_->swapchain.image_format)
    {
        SPECTRA_LOG_WARN("vulkan",
                         "Window " + std::to_string(wctx.id) + " swapchain format ("
                             + std::to_string(wctx.swapchain.image_format)
                             + ") differs from primary ("
                             + std::to_string(initial_window_->swapchain.image_format)
                             + "). Recreating swapchain with primary format.");

        // Force-recreate with the primary's format by destroying and recreating
        // the swapchain.  The surface must support the primary format — if not,
        // this will fail and we bail out.
        auto* prev_active = active_window_;
        active_window_    = &wctx;

        vk::destroy_swapchain(ctx_.device, wctx.swapchain);

        try
        {
            auto vk_msaa   = static_cast<VkSampleCountFlagBits>(msaa_samples_);
            wctx.swapchain = vk::create_swapchain(
                ctx_.device,
                ctx_.physical_device,
                wctx.surface,
                width,
                height,
                ctx_.queue_families.graphics.value(),
                ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
                VK_NULL_HANDLE,
                VK_NULL_HANDLE,
                vk_msaa);
        }
        catch (const std::exception& e)
        {
            SPECTRA_LOG_ERROR(
                "vulkan",
                "Failed to recreate swapchain with primary format: " + std::string(e.what()));
            active_window_ = prev_active;
            return false;
        }

        active_window_ = prev_active;

        if (wctx.swapchain.image_format != initial_window_->swapchain.image_format)
        {
            SPECTRA_LOG_ERROR("vulkan",
                              "Window " + std::to_string(wctx.id)
                                  + " still has mismatched format after recreation — aborting");
            return false;
        }
    }

#if defined(SPECTRA_USE_IMGUI) && defined(SPECTRA_USE_GLFW)
    // Step 3: Initialize per-window ImGui context.
    // Each window gets its own ImGui context for complete isolation.
    auto* prev_imgui_ctx = ImGui::GetCurrentContext();
    auto* prev_active    = active_window_;

    // Section 3F constraint 1: set_active_window so render_pass() returns
    // this window's render pass (not the primary's).
    active_window_ = &wctx;

    ImGuiContext* new_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(new_ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    auto* glfw_win = static_cast<GLFWwindow*>(wctx.glfw_window);
    ImGui_ImplGlfw_InitForVulkan(glfw_win, true);

    // Section 3F constraint 3: use per-window ImageCount
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance                     = ctx_.instance;
    ii.PhysicalDevice               = ctx_.physical_device;
    ii.Device                       = ctx_.device;
    ii.QueueFamily                  = ctx_.queue_families.graphics.value_or(0);
    ii.Queue                        = ctx_.graphics_queue;
    ii.DescriptorPool               = descriptor_pool_;
    ii.MinImageCount                = 2;
    ii.ImageCount                   = static_cast<uint32_t>(wctx.swapchain.images.size());
    ii.PipelineInfoMain.RenderPass  = wctx.swapchain.render_pass;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    // Store the ImGui context handle on the WindowContext so callers can
    // switch to it with ImGui::SetCurrentContext() before each frame.
    wctx.imgui_context = new_ctx;

    SPECTRA_LOG_INFO("imgui",
                     "Per-window ImGui context created for window " + std::to_string(wctx.id));

    // Restore previous ImGui context and active window
    ImGui::SetCurrentContext(prev_imgui_ctx);
    active_window_ = prev_active;
#else
    (void)wctx;
    (void)width;
    (void)height;
#endif

    return true;
}

void VulkanBackend::wait_window_fences(WindowContext& wctx)
{
    if (ctx_.device == VK_NULL_HANDLE || wctx.in_flight_fences.empty())
        return;
    static constexpr uint64_t FENCE_TIMEOUT_NS = 100'000'000;   // 100ms
    vkWaitForFences(ctx_.device,
                    static_cast<uint32_t>(wctx.in_flight_fences.size()),
                    wctx.in_flight_fences.data(),
                    VK_TRUE,
                    FENCE_TIMEOUT_NS);
}

void VulkanBackend::destroy_window_context(WindowContext& wctx)
{
    // Wait for ALL GPU work to complete before destroying sync objects.
    // vkWaitForFences alone is insufficient — semaphores may still be
    // referenced by pending vkQueueSubmit/vkQueuePresentKHR operations.
    if (ctx_.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(ctx_.device);
    }

#if defined(SPECTRA_USE_IMGUI) && defined(SPECTRA_USE_GLFW)
    // Destroy per-window ImGui context (if this window had one).
    // Must happen before Vulkan resource teardown since ImGui holds
    // Vulkan descriptor sets and pipeline references.
    if (wctx.imgui_context)
    {
        auto* prev_ctx = ImGui::GetCurrentContext();
        auto* this_ctx = static_cast<ImGuiContext*>(wctx.imgui_context);
        ImGui::SetCurrentContext(this_ctx);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(this_ctx);
        wctx.imgui_context = nullptr;

        // Restore previous context (unless it was the one we just destroyed)
        ImGui::SetCurrentContext(prev_ctx != this_ctx ? prev_ctx : nullptr);

        SPECTRA_LOG_INFO(
            "imgui",
            "Per-window ImGui context destroyed for window " + std::to_string(wctx.id));
    }
#endif

    // Destroy sync objects
    for (auto sem : wctx.image_available_semaphores)
        vkDestroySemaphore(ctx_.device, sem, nullptr);
    for (auto sem : wctx.render_finished_semaphores)
        vkDestroySemaphore(ctx_.device, sem, nullptr);
    for (auto fence : wctx.in_flight_fences)
        vkDestroyFence(ctx_.device, fence, nullptr);
    wctx.image_available_semaphores.clear();
    wctx.render_finished_semaphores.clear();
    wctx.in_flight_fences.clear();

    // Free command buffers back to the shared pool
    if (!wctx.command_buffers.empty() && command_pool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(ctx_.device,
                             command_pool_,
                             static_cast<uint32_t>(wctx.command_buffers.size()),
                             wctx.command_buffers.data());
        wctx.command_buffers.clear();
    }
    wctx.current_cmd = VK_NULL_HANDLE;

    // Destroy swapchain
    vk::destroy_swapchain(ctx_.device, wctx.swapchain);

    // Destroy surface
    destroy_surface_for(wctx);

    SPECTRA_LOG_INFO("vulkan", "Window context " + std::to_string(wctx.id) + " destroyed");
}

void VulkanBackend::create_command_buffers_for(WindowContext& wctx)
{
    // Free existing command buffers before allocating new ones
    if (!wctx.command_buffers.empty() && command_pool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(ctx_.device,
                             command_pool_,
                             static_cast<uint32_t>(wctx.command_buffers.size()),
                             wctx.command_buffers.data());
    }

    uint32_t count = static_cast<uint32_t>(wctx.swapchain.images.size());
    wctx.command_buffers.resize(count);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = command_pool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = count;

    if (vkAllocateCommandBuffers(ctx_.device, &info, wctx.command_buffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers for window "
                                 + std::to_string(wctx.id));
    }
}

void VulkanBackend::create_sync_objects_for(WindowContext& wctx)
{
    uint32_t count = static_cast<uint32_t>(wctx.swapchain.images.size());
    wctx.image_available_semaphores.resize(count);
    wctx.render_finished_semaphores.resize(count);
    wctx.in_flight_fences.resize(count);

    // Track actual flight frame count for deferred deletion
    if (count > flight_count_)
        flight_count_ = count;

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < count; ++i)
    {
        if (vkCreateSemaphore(ctx_.device, &sem_info, nullptr, &wctx.image_available_semaphores[i])
                != VK_SUCCESS
            || vkCreateSemaphore(ctx_.device,
                                 &sem_info,
                                 nullptr,
                                 &wctx.render_finished_semaphores[i])
                   != VK_SUCCESS
            || vkCreateFence(ctx_.device, &fence_info, nullptr, &wctx.in_flight_fences[i])
                   != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sync objects for window "
                                     + std::to_string(wctx.id));
        }
    }
}

}   // namespace spectra
