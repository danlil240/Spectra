#pragma once

// Pipeline types the experimental WebGPU backend can build.
//
// Kept free of any WebGPU headers (and not gated on SPECTRA_USE_WEBGPU) so the
// supported set can be unit-tested in the default build.  When adding a WGSL
// pipeline to WebGPUBackend::create_pipeline_for_type(), add it here too.

#include <cstddef>
#include <span>

#include "../backend.hpp"

namespace spectra
{

constexpr bool webgpu_supports_pipeline(PipelineType type)
{
    switch (type)
    {
        case PipelineType::Line:
        case PipelineType::Scatter:
        case PipelineType::ScatterColormap:
        case PipelineType::Heatmap:
        case PipelineType::Grid:
        case PipelineType::Overlay:
        case PipelineType::StatFill:
        case PipelineType::Text:
        case PipelineType::TextDepth:
            return true;
        default:
            // No 3D pipeline has a WGSL port yet.  The Renderer skips those
            // series and shows an in-canvas notice.
            return false;
    }
}

// Pick the swapchain format from a surface's supported formats (as reported by
// wgpuSurfaceGetCapabilities, preferred format first; in the browser that is
// navigator.gpu.getPreferredCanvasFormat()).  The first 8-bit UNORM format
// (RGBA8Unorm or BGRA8Unorm) wins so theme colours pass through 1:1, like the
// Vulkan swapchain; an sRGB format would gamma-encode the output and wash the
// theme out.  Falls back to the preferred format, or to `fallback` when the
// list is empty.
//
// Templated on the format type so it can be tested without webgpu.h.
template <typename Format>
constexpr Format webgpu_pick_surface_format(std::span<const Format> formats,
                                            Format                  rgba8_unorm,
                                            Format                  bgra8_unorm,
                                            Format                  fallback)
{
    if (formats.empty())
        return fallback;
    for (Format f : formats)
    {
        if (f == rgba8_unorm || f == bgra8_unorm)
            return f;
    }
    return formats[0];
}

}   // namespace spectra
