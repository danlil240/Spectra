#pragma once

// Pipeline types the experimental WebGPU backend can build.
//
// Kept free of any WebGPU headers (and not gated on SPECTRA_USE_WEBGPU) so the
// supported set can be unit-tested in the default build.  When adding a WGSL
// pipeline to WebGPUBackend::create_pipeline_for_type(), add it here too.

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

}   // namespace spectra
