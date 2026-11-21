// imgui_descriptor_pool.hpp — descriptor pool policy for ImGui's Vulkan backend.
//
// Every ImGui context (one per window) gets a private descriptor pool created by
// the ImGui backend itself, instead of sharing VulkanBackend::descriptor_pool()
// with the renderer.  The renderer allocates frame and series descriptor sets
// from that shared pool until it is full and only then moves on to overflow
// pools; ImGui has no such fallback, so once many figures and series had filled
// the shared pool, opening another window failed vkAllocateDescriptorSets inside
// ImGui_ImplVulkan_AddTexture and the following vkUpdateDescriptorSets on the
// null set crashed in the driver.
//
// Include only where <imgui_impl_vulkan.h> is available (SPECTRA_USE_IMGUI).

#pragma once

#include <imgui_impl_vulkan.h>
#include <spectra/logger.hpp>

#include <cstdint>
#include <string>

namespace spectra
{

// Combined-image-sampler sets per ImGui context: the font atlas, the logo and
// a few preview/user textures, with ample headroom.
inline constexpr uint32_t IMGUI_DESCRIPTOR_POOL_SIZE = 64;

// ImGui calls this after each Vulkan call it makes; without it a failure is
// silently ignored.
inline void imgui_vulkan_check_result(VkResult err)
{
    if (err < 0)
        SPECTRA_LOG_ERROR("imgui",
                          "Vulkan call in the ImGui backend failed: VkResult "
                              + std::to_string(static_cast<int>(err)));
}

// Configure @p ii so the ImGui backend creates and owns its own descriptor pool
// (destroyed by ImGui_ImplVulkan_Shutdown) and reports Vulkan errors.
inline void use_private_imgui_descriptor_pool(ImGui_ImplVulkan_InitInfo& ii)
{
    ii.DescriptorPool     = VK_NULL_HANDLE;
    ii.DescriptorPoolSize = IMGUI_DESCRIPTOR_POOL_SIZE;
    ii.CheckVkResultFn    = &imgui_vulkan_check_result;
}

}   // namespace spectra
