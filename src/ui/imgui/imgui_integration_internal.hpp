// Internal shared header for imgui_integration split files.
// Not part of the public API — only included by imgui_integration*.cpp files.
#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include "ui/window/window_manager.hpp"
    #include "imgui_integration.hpp"

    #include <imgui.h>
    #include <imgui_impl_glfw.h>
    #include <imgui_impl_vulkan.h>
    #include <imgui_internal.h>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/camera.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/logger.hpp>
    #include <spectra/math3d.hpp>
    #include <spectra/series.hpp>
    #include <spectra/series3d.hpp>
    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <string>
    #include <type_traits>
    #include <unordered_map>

    #include "render/vulkan/vk_backend.hpp"

    #include "ui/animation/animation_curve_editor.hpp"
    #include "ui/data/axis_link.hpp"
    #include "ui/input/box_zoom_overlay.hpp"
    #include "ui/commands/command_palette.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/series_clipboard.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/docking/dock_system.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/animation/keyframe_interpolator.hpp"
    #include "ui/overlay/knob_manager.hpp"
    #include "math/data_transform.hpp"
    #include "ui/animation/mode_transition.hpp"
    #include "ui/figures/tab_bar.hpp"
    #include "ui/figures/tab_drag_controller.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/animation/timeline_editor.hpp"
    #include "ui/app/ros2_adapter_state.hpp"
    #include "widgets.hpp"

// Portable conversion: VkRenderPass is a pointer on 64-bit, uint64_t on 32-bit.
// Must be a template so if-constexpr discards the invalid branch.
template <typename H>
static inline uint64_t vk_rp_to_u64(H rp)
{
    if constexpr (std::is_pointer_v<H>)
        return reinterpret_cast<uint64_t>(rp);
    else
        return static_cast<uint64_t>(rp);
}

static inline ImTextureID imgui_texture_id_from_u64(uint64_t bits)
{
    return static_cast<ImTextureID>(bits);
}

template <typename H>
static inline H u64_to_handle(uint64_t bits)
{
    if constexpr (std::is_pointer_v<H>)
        return reinterpret_cast<H>(bits);
    else
        return static_cast<H>(bits);
}

#endif   // SPECTRA_USE_IMGUI
