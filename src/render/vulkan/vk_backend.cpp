#include "vk_backend.hpp"

#include <spectra/logger.hpp>

#include "shader_spirv.hpp"

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

#include <cstring>
#include <iostream>
#include <stdexcept>

// WindowUIContext must be complete for unique_ptr destructor in WindowContext.
// Must come after GLFW includes to avoid macro conflicts with shortcut_manager.hpp.
#include "../../anim/frame_profiler.hpp"
#include "../../ui/window_ui_context.hpp"

namespace spectra
{

// Out-of-line destructor so unique_ptr<WindowUIContext> sees the complete type.
WindowContext::~WindowContext() = default;

VulkanBackend::VulkanBackend() : initial_window_(std::make_unique<WindowContext>())
{
    active_window_ = initial_window_.get();
}

VulkanBackend::~VulkanBackend()
{
    shutdown();
}

bool VulkanBackend::init(bool headless)
{
    headless_ = headless;

    SPECTRA_LOG_INFO(
        "vulkan",
        "Initializing Vulkan backend (headless: " + std::string(headless ? "true" : "false") + ")");

    try
    {
#ifdef NDEBUG
        bool enable_validation = false;
#else
        bool enable_validation = true;
#endif
        SPECTRA_LOG_DEBUG(
            "vulkan",
            "Validation layers: " + std::string(enable_validation ? "true" : "false"));

        ctx_.instance = vk::create_instance(enable_validation, headless_);

        if (enable_validation)
        {
            ctx_.debug_messenger = vk::create_debug_messenger(ctx_.instance);
            SPECTRA_LOG_DEBUG("vulkan", "Debug messenger created");
        }

        // For headless, pick device without surface
        ctx_.physical_device = vk::pick_physical_device(ctx_.instance, active_window_->surface);
        ctx_.queue_families =
            vk::find_queue_families(ctx_.physical_device, active_window_->surface);

        // When not headless, force swapchain extension even though surface doesn't exist yet
        // (surface is created later by GLFW adapter, but device needs the extension at creation
        // time)
        if (!headless_)
        {
            ctx_.queue_families.present = ctx_.queue_families.graphics;
        }
        ctx_.device =
            vk::create_logical_device(ctx_.physical_device, ctx_.queue_families, enable_validation);

        vkGetDeviceQueue(ctx_.device,
                         ctx_.queue_families.graphics.value(),
                         0,
                         &ctx_.graphics_queue);
        if (ctx_.queue_families.has_present())
        {
            vkGetDeviceQueue(ctx_.device,
                             ctx_.queue_families.present.value(),
                             0,
                             &ctx_.present_queue);
        }

        vkGetPhysicalDeviceProperties(ctx_.physical_device, &ctx_.properties);
        vkGetPhysicalDeviceMemoryProperties(ctx_.physical_device, &ctx_.memory_properties);

        // Query alignment for dynamic UBO offsets — round up FrameUBO size
        // to the device's minUniformBufferOffsetAlignment
        {
            VkDeviceSize align = ctx_.properties.limits.minUniformBufferOffsetAlignment;
            if (align == 0)
                align = 1;
            ubo_slot_alignment_ = (sizeof(FrameUBO) + align - 1) & ~(align - 1);
        }

        create_command_pool();
        create_descriptor_pool();

        // Create descriptor set layouts and pipeline layouts
        frame_desc_layout_  = vk::create_frame_descriptor_layout(ctx_.device);
        series_desc_layout_ = vk::create_series_descriptor_layout(ctx_.device);
        pipeline_layout_ =
            vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, series_desc_layout_});

        // Texture descriptor set layout (combined image sampler at binding 0)
        {
            VkDescriptorSetLayoutBinding sampler_binding{};
            sampler_binding.binding         = 0;
            sampler_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_binding.descriptorCount = 1;
            sampler_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 1;
            layout_info.pBindings    = &sampler_binding;

            if (vkCreateDescriptorSetLayout(ctx_.device,
                                            &layout_info,
                                            nullptr,
                                            &texture_desc_layout_)
                != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create texture descriptor set layout");
            }
        }

        // Text pipeline layout: set 0 = frame UBO, set 1 = texture sampler
        text_pipeline_layout_ =
            vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, texture_desc_layout_});

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Spectra] Backend init failed: " << e.what() << "\n";
        return false;
    }
}

void VulkanBackend::wait_idle()
{
    if (ctx_.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(ctx_.device);
    }
}

void VulkanBackend::shutdown()
{
    if (ctx_.device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(ctx_.device);

    // GPU is fully idle — flush all deferred buffer deletions
    flush_pending_buffer_frees(/*force_all=*/true);

    // Destroy pipelines
    for (auto& [id, pipeline] : pipelines_)
    {
        vkDestroyPipeline(ctx_.device, pipeline, nullptr);
    }
    pipelines_.clear();

    // Destroy buffers (GpuBuffer destructors handle Vulkan cleanup)
    buffers_.clear();

    // Destroy textures
    for (auto& [id, tex] : textures_)
    {
        if (tex.sampler != VK_NULL_HANDLE)
            vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)
            vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)
            vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)
            vkFreeMemory(ctx_.device, tex.memory, nullptr);
    }
    textures_.clear();

    // Destroy sync objects and per-window Vulkan resources.
    // If the window was released to WindowManager, it already cleaned up.
    if (active_window_)
    {
        for (auto sem : active_window_->image_available_semaphores)
            vkDestroySemaphore(ctx_.device, sem, nullptr);
        for (auto sem : active_window_->render_finished_semaphores)
            vkDestroySemaphore(ctx_.device, sem, nullptr);
        for (auto fence : active_window_->in_flight_fences)
            vkDestroyFence(ctx_.device, fence, nullptr);
        active_window_->image_available_semaphores.clear();
        active_window_->render_finished_semaphores.clear();
        active_window_->in_flight_fences.clear();
    }

    // Destroy layouts
    if (text_pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, text_pipeline_layout_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
    if (texture_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, texture_desc_layout_, nullptr);
    if (series_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, series_desc_layout_, nullptr);
    if (frame_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, frame_desc_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);

    if (command_pool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx_.device, command_pool_, nullptr);

    vk::destroy_offscreen(ctx_.device, offscreen_);

    if (active_window_)
    {
        vk::destroy_swapchain(ctx_.device, active_window_->swapchain);
        if (active_window_->surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(ctx_.instance, active_window_->surface, nullptr);
            active_window_->surface = VK_NULL_HANDLE;
        }
    }

    vkDestroyDevice(ctx_.device, nullptr);

    if (ctx_.debug_messenger != VK_NULL_HANDLE)
        vk::destroy_debug_messenger(ctx_.instance, ctx_.debug_messenger);

    vkDestroyInstance(ctx_.instance, nullptr);

    ctx_ = {};
}

bool VulkanBackend::create_surface(void* native_window)
{
    if (!native_window)
        return false;

#ifdef SPECTRA_USE_GLFW
    auto*    glfw_window = static_cast<GLFWwindow*>(native_window);
    VkResult result =
        glfwCreateWindowSurface(ctx_.instance, glfw_window, nullptr, &active_window_->surface);
    if (result != VK_SUCCESS)
    {
        std::cerr << "[Spectra] Failed to create Vulkan surface (VkResult=" << result << ")\n";
        return false;
    }

    // Re-query present support for the created surface, but keep device-created queue
    // family indices stable. The logical device was created before surface creation,
    // so it may not contain a separately discovered present family index.
    const auto surface_families =
        vk::find_queue_families(ctx_.physical_device, active_window_->surface);
    if (surface_families.has_present()
        && surface_families.present.value() == ctx_.queue_families.graphics.value())
    {
        ctx_.queue_families.present = ctx_.queue_families.graphics;
        vkGetDeviceQueue(ctx_.device, ctx_.queue_families.present.value(), 0, &ctx_.present_queue);
    }
    else
    {
        if (surface_families.has_present())
        {
            SPECTRA_LOG_WARN("vulkan",
                             "Surface present queue family differs from device queue family; "
                             "falling back to graphics queue for present operations");
        }
        ctx_.queue_families.present = ctx_.queue_families.graphics;
        ctx_.present_queue          = ctx_.graphics_queue;
    }

    // Ensure present queue is always valid.
    if (ctx_.present_queue == VK_NULL_HANDLE)
    {
        ctx_.present_queue = ctx_.graphics_queue;
    }

    return true;
#else
    (void)native_window;
    return false;
#endif
}

bool VulkanBackend::create_swapchain(uint32_t width, uint32_t height)
{
    if (active_window_->surface == VK_NULL_HANDLE)
        return false;

    try
    {
        auto vk_msaa              = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        active_window_->swapchain = vk::create_swapchain(
            ctx_.device,
            ctx_.physical_device,
            active_window_->surface,
            width,
            height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            vk_msaa);
        create_command_buffers();
        create_sync_objects();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Spectra] Swapchain creation failed: " << e.what() << "\n";
        return false;
    }
}

bool VulkanBackend::recreate_swapchain(uint32_t width, uint32_t height)
{
    SPECTRA_LOG_INFO(
        "vulkan",
        "recreate_swapchain called: " + std::to_string(width) + "x" + std::to_string(height));

    // Wait only on in-flight fences instead of vkDeviceWaitIdle (much faster)
    if (!active_window_->in_flight_fences.empty())
    {
        SPECTRA_LOG_DEBUG("vulkan",
                          "Waiting for " + std::to_string(active_window_->in_flight_fences.size())
                              + " in-flight fences before swapchain recreation");
        auto wait_start = std::chrono::high_resolution_clock::now();
        vkWaitForFences(ctx_.device,
                        static_cast<uint32_t>(active_window_->in_flight_fences.size()),
                        active_window_->in_flight_fences.data(),
                        VK_TRUE,
                        UINT64_MAX);
        auto wait_end = std::chrono::high_resolution_clock::now();
        auto wait_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(wait_end - wait_start);
        SPECTRA_LOG_DEBUG(
            "vulkan",
            "Fence wait completed in " + std::to_string(wait_duration.count()) + "ms");
    }

    SPECTRA_LOG_DEBUG("vulkan", "Starting swapchain recreation...");
    auto         old_swapchain = active_window_->swapchain.swapchain;
    auto         old_context   = active_window_->swapchain;   // Copy the entire context
    VkRenderPass reuse_rp      = old_context.render_pass;     // Reuse — format doesn't change

    try
    {
        SPECTRA_LOG_DEBUG("vulkan", "Creating new swapchain...");
        auto vk_msaa              = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        active_window_->swapchain = vk::create_swapchain(
            ctx_.device,
            ctx_.physical_device,
            active_window_->surface,
            width,
            height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            old_swapchain,
            reuse_rp,
            vk_msaa);
        SPECTRA_LOG_INFO(
            "vulkan",
            "New swapchain created: " + std::to_string(active_window_->swapchain.extent.width) + "x"
                + std::to_string(active_window_->swapchain.extent.height));

        // Destroy the old swapchain context (skip render pass — we reused it)
        SPECTRA_LOG_DEBUG("vulkan", "Destroying old swapchain...");
        vk::destroy_swapchain(ctx_.device, old_context, /*skip_render_pass=*/true);

        // Recreate sync objects only if image count changed (rare during resize)
        if (active_window_->swapchain.images.size() != old_context.images.size())
        {
            SPECTRA_LOG_DEBUG("vulkan",
                              "Image count changed " + std::to_string(old_context.images.size())
                                  + " -> " + std::to_string(active_window_->swapchain.images.size())
                                  + ", recreating sync objects");
            for (auto sem : active_window_->image_available_semaphores)
                vkDestroySemaphore(ctx_.device, sem, nullptr);
            for (auto sem : active_window_->render_finished_semaphores)
                vkDestroySemaphore(ctx_.device, sem, nullptr);
            for (auto fence : active_window_->in_flight_fences)
                vkDestroyFence(ctx_.device, fence, nullptr);
            active_window_->image_available_semaphores.clear();
            active_window_->render_finished_semaphores.clear();
            active_window_->in_flight_fences.clear();
            create_sync_objects();
        }
        active_window_->current_flight_frame  = 0;
        active_window_->swapchain_invalidated = false;

        SPECTRA_LOG_INFO("vulkan", "Swapchain recreation completed successfully");
        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Swapchain recreation failed: " + std::string(e.what()));
        return false;
    }
}

bool VulkanBackend::create_offscreen_framebuffer(uint32_t width, uint32_t height)
{
    try
    {
        vk::destroy_offscreen(ctx_.device, offscreen_);
        auto vk_msaa = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        offscreen_   = vk::create_offscreen_framebuffer(ctx_.device,
                                                      ctx_.physical_device,
                                                      width,
                                                      height,
                                                      vk_msaa);
        create_command_buffers();
        create_sync_objects();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Spectra] Offscreen framebuffer creation failed: " << e.what() << "\n";
        return false;
    }
}

PipelineHandle VulkanBackend::create_pipeline(PipelineType type)
{
    PipelineHandle h;
    h.id = next_pipeline_id_++;

    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE)
    {
        // Render pass not yet available — store placeholder, will be created lazily
        pipelines_[h.id]      = VK_NULL_HANDLE;
        pipeline_types_[h.id] = type;
        return h;
    }

    pipelines_[h.id]        = create_pipeline_for_type(type, rp);
    pipeline_layouts_[h.id] = (type == PipelineType::Text || type == PipelineType::TextDepth)
                                  ? text_pipeline_layout_
                                  : pipeline_layout_;
    return h;
}

VkPipeline VulkanBackend::create_pipeline_for_type(PipelineType type, VkRenderPass rp)
{
    vk::PipelineConfig cfg;
    cfg.render_pass     = rp;
    cfg.pipeline_layout = pipeline_layout_;
    cfg.enable_blending = true;
    cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    switch (type)
    {
        case PipelineType::Line:
            cfg.vert_spirv      = shaders::line_vert;
            cfg.vert_spirv_size = shaders::line_vert_size;
            cfg.frag_spirv      = shaders::line_frag;
            cfg.frag_spirv_size = shaders::line_frag_size;
            break;
        case PipelineType::Scatter:
            cfg.vert_spirv      = shaders::scatter_vert;
            cfg.vert_spirv_size = shaders::scatter_vert_size;
            cfg.frag_spirv      = shaders::scatter_frag;
            cfg.frag_spirv_size = shaders::scatter_frag_size;
            break;
        case PipelineType::Grid:
            cfg.vert_spirv      = shaders::grid_vert;
            cfg.vert_spirv_size = shaders::grid_vert_size;
            cfg.frag_spirv      = shaders::grid_frag;
            cfg.frag_spirv_size = shaders::grid_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            // Grid uses vec2 vertex attribute for line endpoints
            cfg.vertex_bindings.push_back({0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            break;
        case PipelineType::Line3D:
            cfg.vert_spirv         = shaders::line3d_vert;
            cfg.vert_spirv_size    = shaders::line3d_vert_size;
            cfg.frag_spirv         = shaders::line3d_frag;
            cfg.frag_spirv_size    = shaders::line3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            break;
        case PipelineType::Scatter3D:
            cfg.vert_spirv         = shaders::scatter3d_vert;
            cfg.vert_spirv_size    = shaders::scatter3d_vert_size;
            cfg.frag_spirv         = shaders::scatter3d_frag;
            cfg.frag_spirv_size    = shaders::scatter3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            break;
        case PipelineType::Grid3D:
            cfg.vert_spirv         = shaders::grid3d_vert;
            cfg.vert_spirv_size    = shaders::grid3d_vert_size;
            cfg.frag_spirv         = shaders::grid3d_frag;
            cfg.frag_spirv_size    = shaders::grid3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Grid3D uses vec3 vertex attribute for line endpoints
            cfg.vertex_bindings.push_back({0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            break;
        case PipelineType::GridOverlay3D:
            cfg.vert_spirv         = shaders::grid3d_vert;
            cfg.vert_spirv_size    = shaders::grid3d_vert_size;
            cfg.frag_spirv         = shaders::grid3d_frag;
            cfg.frag_spirv_size    = shaders::grid3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = false;
            cfg.enable_depth_write = false;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            break;
        case PipelineType::Surface3D:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Surface vertex: {x,y,z, nx,ny,nz} = 6 floats per vertex, 2 attributes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        case PipelineType::Mesh3D:
            cfg.vert_spirv         = shaders::mesh3d_vert;
            cfg.vert_spirv_size    = shaders::mesh3d_vert_size;
            cfg.frag_spirv         = shaders::mesh3d_frag;
            cfg.frag_spirv_size    = shaders::mesh3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Mesh vertex: same layout as surface {x,y,z, nx,ny,nz}
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        // ── Wireframe 3D pipeline variants (line topology with vertex buffer) ──
        case PipelineType::SurfaceWireframe3D:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::SurfaceWireframe3D_Transparent:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        // ── Transparent 3D pipeline variants (depth test ON, depth write OFF) ──
        case PipelineType::Line3D_Transparent:
            cfg.vert_spirv         = shaders::line3d_vert;
            cfg.vert_spirv_size    = shaders::line3d_vert_size;
            cfg.frag_spirv         = shaders::line3d_frag;
            cfg.frag_spirv_size    = shaders::line3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;   // Don't write depth for transparent
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case PipelineType::Scatter3D_Transparent:
            cfg.vert_spirv         = shaders::scatter3d_vert;
            cfg.vert_spirv_size    = shaders::scatter3d_vert_size;
            cfg.frag_spirv         = shaders::scatter3d_frag;
            cfg.frag_spirv_size    = shaders::scatter3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case PipelineType::Surface3D_Transparent:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::Mesh3D_Transparent:
            cfg.vert_spirv         = shaders::mesh3d_vert;
            cfg.vert_spirv_size    = shaders::mesh3d_vert_size;
            cfg.frag_spirv         = shaders::mesh3d_frag;
            cfg.frag_spirv_size    = shaders::mesh3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::Heatmap:
            return VK_NULL_HANDLE;   // Not yet implemented
        case PipelineType::Overlay:
            cfg.vert_spirv      = shaders::grid_vert;
            cfg.vert_spirv_size = shaders::grid_vert_size;
            cfg.frag_spirv      = shaders::grid_frag;
            cfg.frag_spirv_size = shaders::grid_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            // Same vec2 vertex attribute as Grid, but triangle topology for filled shapes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            break;
        case PipelineType::Arrow3D:
            cfg.vert_spirv         = shaders::arrow3d_vert;
            cfg.vert_spirv_size    = shaders::arrow3d_vert_size;
            cfg.frag_spirv         = shaders::arrow3d_frag;
            cfg.frag_spirv_size    = shaders::arrow3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Arrow vertex: {x,y,z, nx,ny,nz} = 6 floats per vertex, 2 attributes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        case PipelineType::Text:
            cfg.pipeline_layout    = text_pipeline_layout_;
            cfg.vert_spirv         = shaders::text_vert;
            cfg.vert_spirv_size    = shaders::text_vert_size;
            cfg.frag_spirv         = shaders::text_frag;
            cfg.frag_spirv_size    = shaders::text_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = false;
            cfg.enable_depth_write = false;
            // TextVertex: {float x, y, z, float u, v, uint32_t col} = 24 bytes
            cfg.vertex_bindings.push_back(
                {0, sizeof(float) * 5 + sizeof(uint32_t), VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back(
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position (x, y, z)
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});   // uv
            cfg.vertex_attributes.push_back(
                {2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(sizeof(float) * 5)});   // color
            break;
        case PipelineType::TextDepth:
            cfg.pipeline_layout    = text_pipeline_layout_;
            cfg.vert_spirv         = shaders::text_vert;
            cfg.vert_spirv_size    = shaders::text_vert_size;
            cfg.frag_spirv         = shaders::text_frag;
            cfg.frag_spirv_size    = shaders::text_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            // TextVertex: {float x, y, z, float u, v, uint32_t col} = 24 bytes
            cfg.vertex_bindings.push_back(
                {0, sizeof(float) * 5 + sizeof(uint32_t), VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back(
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position (x, y, z)
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});   // uv
            cfg.vertex_attributes.push_back(
                {2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(sizeof(float) * 5)});   // color
            break;
    }

    // All pipelines must match the render pass sample count
    cfg.msaa_samples = static_cast<VkSampleCountFlagBits>(msaa_samples_);

    try
    {
        return vk::create_graphics_pipeline(ctx_.device, cfg);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Spectra] Pipeline creation failed: " << e.what() << "\n";
        return VK_NULL_HANDLE;
    }
}

void VulkanBackend::ensure_pipelines()
{
    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE)
        return;

    for (auto& [id, pipeline] : pipelines_)
    {
        if (pipeline == VK_NULL_HANDLE)
        {
            auto it = pipeline_types_.find(id);
            if (it != pipeline_types_.end())
            {
                pipeline = create_pipeline_for_type(it->second, rp);
                pipeline_layouts_[id] =
                    (it->second == PipelineType::Text || it->second == PipelineType::TextDepth)
                        ? text_pipeline_layout_
                        : pipeline_layout_;
            }
        }
    }
}

BufferHandle VulkanBackend::create_buffer(BufferUsage usage, size_t size_bytes)
{
    VkBufferUsageFlags    vk_usage  = 0;
    VkMemoryPropertyFlags mem_props = 0;

    switch (usage)
    {
        case BufferUsage::Vertex:
            vk_usage  = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Index:
            vk_usage  = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Uniform:
            vk_usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            // Allocate enough room for UBO_MAX_SLOTS dynamic slots
            size_bytes = static_cast<size_t>(ubo_slot_alignment_) * UBO_MAX_SLOTS;
            break;
        case BufferUsage::Storage:
            vk_usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Staging:
            vk_usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
    }

    auto buf = vk::GpuBuffer::create(ctx_.device,
                                     ctx_.physical_device,
                                     static_cast<VkDeviceSize>(size_bytes),
                                     vk_usage,
                                     mem_props);

    BufferHandle h;
    h.id = next_buffer_id_++;

    BufferEntry entry;
    entry.gpu_buffer = std::move(buf);
    entry.usage      = usage;

    // Allocate descriptor set for UBO or SSBO buffers
    if (usage == BufferUsage::Uniform)
    {
        entry.descriptor_set = allocate_descriptor_set(frame_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE)
        {
            // Descriptor range = one aligned slot (dynamic offset selects the slot)
            update_ubo_descriptor(entry.descriptor_set,
                                  entry.gpu_buffer.buffer(),
                                  ubo_slot_alignment_);
        }
    }
    else if (usage == BufferUsage::Storage)
    {
        entry.descriptor_set = allocate_descriptor_set(series_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE)
        {
            update_ssbo_descriptor(entry.descriptor_set,
                                   entry.gpu_buffer.buffer(),
                                   static_cast<VkDeviceSize>(size_bytes));
        }
    }

    buffers_.emplace(h.id, std::move(entry));
    return h;
}

void VulkanBackend::destroy_buffer(BufferHandle handle)
{
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end())
    {
        // Defer both the VkBuffer destruction and descriptor set free.
        // The entry is stamped with the current frame counter and will only
        // be freed once flight_count_ frames have elapsed, guaranteeing
        // every in-flight command buffer has completed.
        // VUID-vkDestroyBuffer-buffer-00922 / VUID-vkFreeDescriptorSets-00309
        pending_buffer_frees_.push_back({std::move(it->second), frame_counter_});
        buffers_.erase(it);
    }
}

void VulkanBackend::flush_pending_buffer_frees(bool force_all)
{
    if (pending_buffer_frees_.empty())
        return;

    // Only free entries that are old enough: destroyed at least
    // flight_count_ frames ago, so every flight slot has cycled.
    uint64_t safe_frame = (frame_counter_ > flight_count_) ? frame_counter_ - flight_count_ : 0;

    size_t write = 0;
    for (size_t read = 0; read < pending_buffer_frees_.size(); ++read)
    {
        auto& d = pending_buffer_frees_[read];
        if (force_all || d.frame_destroyed <= safe_frame)
        {
            // Free descriptor set first (it references the buffer)
            if (d.entry.descriptor_set != VK_NULL_HANDLE && descriptor_pool_ != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(ctx_.device, descriptor_pool_, 1, &d.entry.descriptor_set);
                d.entry.descriptor_set = VK_NULL_HANDLE;
            }
            // GpuBuffer destructor fires when d goes out of scope (overwritten or erased)
        }
        else
        {
            // Keep this entry — not old enough yet
            if (write != read)
                pending_buffer_frees_[write] = std::move(pending_buffer_frees_[read]);
            ++write;
        }
    }
    pending_buffer_frees_.resize(write);
}

void VulkanBackend::advance_deferred_deletion()
{
    ++frame_counter_;
    flush_pending_buffer_frees();
}

void VulkanBackend::upload_buffer(BufferHandle handle,
                                  const void*  data,
                                  size_t       size_bytes,
                                  size_t       offset)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;
    auto& buf   = entry.gpu_buffer;

    // For dynamic UBO buffers, write to the next aligned slot
    if (entry.usage == BufferUsage::Uniform)
    {
        uint32_t slot_size = static_cast<uint32_t>(ubo_slot_alignment_);
        if (ubo_next_offset_ + slot_size > slot_size * UBO_MAX_SLOTS)
        {
            ubo_next_offset_ = 0;   // wrap around (shouldn't happen with 64 slots)
        }
        ubo_bound_offset_ = ubo_next_offset_;
        buf.upload(data,
                   static_cast<VkDeviceSize>(size_bytes),
                   static_cast<VkDeviceSize>(ubo_next_offset_));
        ubo_next_offset_ += slot_size;
        return;
    }

    // For host-visible buffers, direct upload
    try
    {
        buf.upload(data, static_cast<VkDeviceSize>(size_bytes), static_cast<VkDeviceSize>(offset));
    }
    catch (...)
    {
        // For device-local buffers, use staging
        vk::staging_upload(ctx_.device,
                           ctx_.physical_device,
                           command_pool_,
                           ctx_.graphics_queue,
                           buf.buffer(),
                           data,
                           static_cast<VkDeviceSize>(size_bytes),
                           static_cast<VkDeviceSize>(offset));
    }
}

TextureHandle VulkanBackend::create_texture(uint32_t       width,
                                            uint32_t       height,
                                            const uint8_t* rgba_data)
{
    TextureHandle h;
    h.id = next_texture_id_++;
    TextureEntry tex{};

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Create VkImage
    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent        = {width, height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx_.device, &image_info, nullptr, &tex.image) != VK_SUCCESS)
    {
        textures_[h.id] = {};
        return h;
    }

    // Allocate device memory for the image
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx_.device, tex.image, &mem_reqs);

    uint32_t mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < ctx_.memory_properties.memoryTypeCount; ++i)
    {
        if ((mem_reqs.memoryTypeBits & (1 << i))
            && (ctx_.memory_properties.memoryTypes[i].propertyFlags
                & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == UINT32_MAX)
    {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type_idx;

    if (vkAllocateMemory(ctx_.device, &alloc_info, nullptr, &tex.memory) != VK_SUCCESS)
    {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }
    vkBindImageMemory(ctx_.device, tex.image, tex.memory, 0);

    // Upload pixel data via staging buffer
    if (rgba_data)
    {
        auto staging = vk::GpuBuffer::create(
            ctx_.device,
            ctx_.physical_device,
            image_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.upload(rgba_data, image_size);

        // One-shot command buffer for layout transition + copy
        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool        = command_pool_;
        cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(ctx_.device, &cmd_alloc, &cmd);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = tex.image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {width, height, 1};

        vkCmdCopyBufferToImage(cmd,
                               staging.buffer(),
                               tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);

        // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);

        vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);
        staging.destroy();
    }

    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = tex.image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(ctx_.device, &view_info, nullptr, &tex.view) != VK_SUCCESS)
    {
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Create sampler
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_LINEAR;
    sampler_info.minFilter    = VK_FILTER_LINEAR;
    sampler_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod       = 1.0f;

    if (vkCreateSampler(ctx_.device, &sampler_info, nullptr, &tex.sampler) != VK_SUCCESS)
    {
        vkDestroyImageView(ctx_.device, tex.view, nullptr);
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Allocate and update descriptor set for this texture
    tex.descriptor_set = allocate_descriptor_set(texture_desc_layout_);
    if (tex.descriptor_set != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo img_info{};
        img_info.sampler     = tex.sampler;
        img_info.imageView   = tex.view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = tex.descriptor_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;

        vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
    }

    textures_[h.id] = tex;
    return h;
}

void VulkanBackend::destroy_texture(TextureHandle handle)
{
    auto it = textures_.find(handle.id);
    if (it != textures_.end())
    {
        auto& tex = it->second;
        if (tex.sampler != VK_NULL_HANDLE)
            vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)
            vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)
            vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)
            vkFreeMemory(ctx_.device, tex.memory, nullptr);
        textures_.erase(it);
    }
}

bool VulkanBackend::begin_frame(FrameProfiler* profiler)
{
    // Reset dynamic UBO slot allocator for this frame
    ubo_next_offset_  = 0;
    ubo_bound_offset_ = 0;

    if (headless_)
    {
        // For offscreen, just allocate a command buffer
        if (active_window_->command_buffers.empty())
        {
            SPECTRA_LOG_ERROR("vulkan", "begin_frame: no command buffers for headless");
            return false;
        }
        active_window_->current_cmd = active_window_->command_buffers[0];

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(active_window_->current_cmd, &begin_info);
        return true;
    }

    // Windowed mode — wait for this slot's previous work to finish
    if (profiler)
        profiler->begin_stage("vk_wait_fences");
    VkResult fence_status =
        vkWaitForFences(ctx_.device,
                        1,
                        &active_window_->in_flight_fences[active_window_->current_flight_frame],
                        VK_TRUE,
                        UINT64_MAX);
    if (profiler)
        profiler->end_stage("vk_wait_fences");
    if (fence_status == VK_ERROR_DEVICE_LOST)
    {
        device_lost_ = true;
        throw std::runtime_error("Vulkan device lost - cannot continue rendering");
    }

    // Acquire next swapchain image BEFORE resetting fence
    if (profiler)
        profiler->begin_stage("vk_acquire");
    VkResult result = vkAcquireNextImageKHR(
        ctx_.device,
        active_window_->swapchain.swapchain,
        UINT64_MAX,
        active_window_->image_available_semaphores[active_window_->current_flight_frame],
        VK_NULL_HANDLE,
        &active_window_->current_image_index);
    if (profiler)
        profiler->end_stage("vk_acquire");

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return false;   // Swapchain truly unusable — caller must recreate
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        // Image is still valid and presentable, just not optimal for the
        // current surface size. Continue rendering (stretched > black flash).
        // The main loop debounce will recreate when resize stabilizes.
        active_window_->swapchain_dirty = true;
    }

    // Only reset fence after successful acquisition
    vkResetFences(ctx_.device,
                  1,
                  &active_window_->in_flight_fences[active_window_->current_flight_frame]);

    active_window_->current_cmd =
        active_window_->command_buffers[active_window_->current_flight_frame];
    vkResetCommandBuffer(active_window_->current_cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(active_window_->current_cmd, &begin_info);

    return true;
}

void VulkanBackend::end_frame(FrameProfiler* profiler)
{
    vkEndCommandBuffer(active_window_->current_cmd);

    if (headless_)
    {
        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &active_window_->current_cmd;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);
        return;
    }

    // Windowed submit
    // image_available: indexed by active_window_->current_flight_frame (matches acquire)
    // render_finished: indexed by active_window_->current_image_index (tied to swapchain image
    // lifecycle —
    //   only reused when that image is re-acquired, guaranteeing the previous present completed)
    VkSemaphore wait_semaphores[] = {
        active_window_->image_available_semaphores[active_window_->current_flight_frame]};
    VkSemaphore signal_semaphores[] = {
        active_window_->render_finished_semaphores[active_window_->current_image_index]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = wait_semaphores;
    submit.pWaitDstStageMask    = wait_stages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &active_window_->current_cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signal_semaphores;

    if (profiler)
        profiler->begin_stage("vk_submit");
    vkQueueSubmit(ctx_.graphics_queue,
                  1,
                  &submit,
                  active_window_->in_flight_fences[active_window_->current_flight_frame]);
    if (profiler)
        profiler->end_stage("vk_submit");

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signal_semaphores;
    present.swapchainCount     = 1;
    present.pSwapchains        = &active_window_->swapchain.swapchain;
    present.pImageIndices      = &active_window_->current_image_index;

    if (profiler)
        profiler->begin_stage("vk_present");
    VkResult result = vkQueuePresentKHR(ctx_.present_queue, &present);
    if (profiler)
        profiler->end_stage("vk_present");

    active_window_->current_flight_frame =
        (active_window_->current_flight_frame + 1)
        % static_cast<uint32_t>(active_window_->in_flight_fences.size());

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        active_window_->swapchain_dirty       = true;
        active_window_->swapchain_invalidated = true;
        SPECTRA_LOG_DEBUG("vulkan", "end_frame: present returned OUT_OF_DATE");
    }
    else if (result == VK_SUBOPTIMAL_KHR)
    {
        active_window_->swapchain_dirty = true;
        SPECTRA_LOG_DEBUG("vulkan", "end_frame: present returned SUBOPTIMAL");
    }
    else if (result != VK_SUCCESS)
    {
        SPECTRA_LOG_ERROR(
            "vulkan",
            "end_frame: present failed with result " + std::to_string(static_cast<int>(result)));
    }
}

void VulkanBackend::begin_render_pass(const Color& clear_color)
{
    VkClearValue clear_values[2]{};
    clear_values[0].color        = {{clear_color.r, clear_color.g, clear_color.b, clear_color.a}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.clearValueCount = 2;
    info.pClearValues    = clear_values;

    if (headless_)
    {
        info.renderPass  = offscreen_.render_pass;
        info.framebuffer = offscreen_.framebuffer;
        info.renderArea  = {{0, 0}, offscreen_.extent};
    }
    else
    {
        info.renderPass = active_window_->swapchain.render_pass;
        info.framebuffer =
            active_window_->swapchain.framebuffers[active_window_->current_image_index];
        info.renderArea = {{0, 0}, active_window_->swapchain.extent};
    }

    vkCmdBeginRenderPass(active_window_->current_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBackend::end_render_pass()
{
    vkCmdEndRenderPass(active_window_->current_cmd);
}

void VulkanBackend::bind_pipeline(PipelineHandle handle)
{
    auto it = pipelines_.find(handle.id);
    if (it != pipelines_.end() && it->second != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(active_window_->current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
        auto layout_it = pipeline_layouts_.find(handle.id);
        current_pipeline_layout_ =
            (layout_it != pipeline_layouts_.end()) ? layout_it->second : pipeline_layout_;
    }
}

void VulkanBackend::bind_buffer(BufferHandle handle, uint32_t binding)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;

    VkPipelineLayout layout =
        current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
    if (entry.usage == BufferUsage::Uniform && entry.descriptor_set != VK_NULL_HANDLE)
    {
        uint32_t dynamic_offset = ubo_bound_offset_;
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                0,
                                1,
                                &entry.descriptor_set,
                                1,
                                &dynamic_offset);
    }
    else if (entry.usage == BufferUsage::Storage && entry.descriptor_set != VK_NULL_HANDLE)
    {
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                1,
                                1,
                                &entry.descriptor_set,
                                0,
                                nullptr);
    }
    else if (entry.usage == BufferUsage::Vertex)
    {
        // Only actual vertex buffers may be bound as vertex buffers.
        VkBuffer     bufs[]    = {entry.gpu_buffer.buffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(active_window_->current_cmd, binding, 1, bufs, offsets);
    }
    // Storage/Uniform with NULL descriptor: silently skip (pool exhausted).
}

void VulkanBackend::bind_index_buffer(BufferHandle handle)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;
    vkCmdBindIndexBuffer(active_window_->current_cmd,
                         entry.gpu_buffer.buffer(),
                         0,
                         VK_INDEX_TYPE_UINT32);
}

void VulkanBackend::bind_texture(TextureHandle handle, uint32_t /*binding*/)
{
    auto it = textures_.find(handle.id);
    if (it == textures_.end())
        return;

    auto& tex = it->second;
    if (tex.descriptor_set != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout =
            current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                1,
                                1,
                                &tex.descriptor_set,
                                0,
                                nullptr);
    }
}

void VulkanBackend::push_constants(const SeriesPushConstants& pc)
{
    VkPipelineLayout layout =
        current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
    vkCmdPushConstants(active_window_->current_cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(SeriesPushConstants),
                       &pc);
}

void VulkanBackend::set_viewport(float x, float y, float width, float height)
{
    VkViewport vp{};
    vp.x        = x;
    vp.y        = y;
    vp.width    = width;
    vp.height   = height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(active_window_->current_cmd, 0, 1, &vp);
}

void VulkanBackend::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(active_window_->current_cmd, 0, 1, &scissor);
}

void VulkanBackend::set_line_width(float width)
{
    vkCmdSetLineWidth(active_window_->current_cmd, width);
}

void VulkanBackend::draw(uint32_t vertex_count, uint32_t first_vertex)
{
    vkCmdDraw(active_window_->current_cmd, vertex_count, 1, first_vertex, 0);
}

void VulkanBackend::draw_instanced(uint32_t vertex_count,
                                   uint32_t instance_count,
                                   uint32_t first_vertex)
{
    vkCmdDraw(active_window_->current_cmd, vertex_count, instance_count, first_vertex, 0);
}

void VulkanBackend::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset)
{
    vkCmdDrawIndexed(active_window_->current_cmd, index_count, 1, first_index, vertex_offset, 0);
}

bool VulkanBackend::readback_framebuffer(uint8_t* out_rgba, uint32_t width, uint32_t height)
{
    // Determine source image and its current layout
    VkImage       src_image  = VK_NULL_HANDLE;
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (headless_)
    {
        src_image  = offscreen_.color_image;
        src_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    else
    {
        if (active_window_->swapchain.images.empty())
            return false;
        src_image = active_window_->swapchain.images[active_window_->swapchain.current_image_index];
        src_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    if (src_image == VK_NULL_HANDLE)
        return false;

    vkQueueWaitIdle(ctx_.graphics_queue);

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;

    auto staging = vk::GpuBuffer::create(
        ctx_.device,
        ctx_.physical_device,
        buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Record copy command
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = command_pool_;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx_.device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition source image to TRANSFER_SRC_OPTIMAL if needed
    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = src_layout;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd,
                           src_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer(),
                           1,
                           &region);

    // Transition back to original layout if we changed it
    if (src_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                       = src_layout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = src_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.graphics_queue);

    vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);

    // Read back from staging buffer (GpuBuffer auto-maps host-visible memory)
    staging.read(out_rgba, buffer_size);
    staging.destroy();

    // Swapchain uses BGRA format — swizzle to RGBA for PNG export
    if (!headless_)
    {
        for (uint32_t i = 0; i < width * height; ++i)
        {
            std::swap(out_rgba[i * 4 + 0], out_rgba[i * 4 + 2]);   // B↔R
        }
    }

    return true;
}

uint32_t VulkanBackend::swapchain_width() const
{
    if (headless_)
        return offscreen_.extent.width;
    return active_window_->swapchain.extent.width;
}

uint32_t VulkanBackend::swapchain_height() const
{
    if (headless_)
        return offscreen_.extent.height;
    return active_window_->swapchain.extent.height;
}

VkRenderPass VulkanBackend::render_pass() const
{
    if (headless_)
        return offscreen_.render_pass;
    return active_window_->swapchain.render_pass;
}

// --- Private helpers ---

void VulkanBackend::create_command_pool()
{
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = ctx_.queue_families.graphics.value();

    if (vkCreateCommandPool(ctx_.device, &info, nullptr, &command_pool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }
}

void VulkanBackend::create_command_buffers()
{
    // Free existing command buffers before allocating new ones (prevents leak on resize)
    if (!active_window_->command_buffers.empty() && command_pool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(ctx_.device,
                             command_pool_,
                             static_cast<uint32_t>(active_window_->command_buffers.size()),
                             active_window_->command_buffers.data());
    }

    uint32_t count = headless_ ? 1 : static_cast<uint32_t>(active_window_->swapchain.images.size());
    active_window_->command_buffers.resize(count);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = command_pool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = count;

    if (vkAllocateCommandBuffers(ctx_.device, &info, active_window_->command_buffers.data())
        != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void VulkanBackend::create_sync_objects()
{
    if (headless_)
        return;

    // Use one set of sync objects per swapchain image to prevent
    // semaphore reuse while the presentation engine still holds them.
    uint32_t count = static_cast<uint32_t>(active_window_->swapchain.images.size());
    active_window_->image_available_semaphores.resize(count);
    active_window_->render_finished_semaphores.resize(count);
    active_window_->in_flight_fences.resize(count);

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
        if (vkCreateSemaphore(ctx_.device,
                              &sem_info,
                              nullptr,
                              &active_window_->image_available_semaphores[i])
                != VK_SUCCESS
            || vkCreateSemaphore(ctx_.device,
                                 &sem_info,
                                 nullptr,
                                 &active_window_->render_finished_semaphores[i])
                   != VK_SUCCESS
            || vkCreateFence(ctx_.device,
                             &fence_info,
                             nullptr,
                             &active_window_->in_flight_fences[i])
                   != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sync objects");
        }
    }
}

void VulkanBackend::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32},
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 256;
    info.poolSizeCount = 3;
    info.pPoolSizes    = pool_sizes;

    if (vkCreateDescriptorPool(ctx_.device, &info, nullptr, &descriptor_pool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

VkDescriptorSet VulkanBackend::allocate_descriptor_set(VkDescriptorSetLayout layout)
{
    if (descriptor_pool_ == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx_.device, &alloc_info, &set) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return set;
}

void VulkanBackend::update_ubo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size)
{
    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

void VulkanBackend::update_ssbo_descriptor(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size)
{
    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

// --- Multi-window helpers ---

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
    if (!wctx.glfw_window)
    {
        SPECTRA_LOG_ERROR("vulkan", "init_window_context: no GLFW window set");
        return false;
    }

    // Save and restore active window so callers don't need to worry about it
    auto* prev_active = active_window_;

    try
    {
#ifdef SPECTRA_USE_GLFW
        auto*    glfw_win = static_cast<GLFWwindow*>(wctx.glfw_window);
        VkResult result = glfwCreateWindowSurface(ctx_.instance, glfw_win, nullptr, &wctx.surface);
        if (result != VK_SUCCESS)
        {
            SPECTRA_LOG_ERROR("vulkan",
                              "init_window_context: surface creation failed (VkResult="
                                  + std::to_string(result) + ")");
            active_window_ = prev_active;
            return false;
        }
#else
        SPECTRA_LOG_ERROR("vulkan", "init_window_context: GLFW not available");
        active_window_ = prev_active;
        return false;
#endif

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
    ii.Instance       = ctx_.instance;
    ii.PhysicalDevice = ctx_.physical_device;
    ii.Device         = ctx_.device;
    ii.QueueFamily    = ctx_.queue_families.graphics.value_or(0);
    ii.Queue          = ctx_.graphics_queue;
    ii.DescriptorPool = descriptor_pool_;
    ii.MinImageCount  = 2;
    ii.ImageCount     = static_cast<uint32_t>(wctx.swapchain.images.size());
    ii.RenderPass     = wctx.swapchain.render_pass;
    ii.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);
    ImGui_ImplVulkan_CreateFontsTexture();

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
    if (wctx.surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(ctx_.instance, wctx.surface, nullptr);
        wctx.surface = VK_NULL_HANDLE;
    }

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
