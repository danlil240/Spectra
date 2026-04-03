#include "series_type_registry.hpp"

#include <algorithm>

#include "spectra/logger.hpp"

namespace spectra
{

void SeriesTypeRegistry::register_type(const std::string&        type_name,
                                       const CustomPipelineDesc& pipeline_desc,
                                       CustomUploadFn            upload_fn,
                                       CustomDrawFn              draw_fn,
                                       CustomBoundsFn            bounds_fn,
                                       CustomCleanupFn           cleanup_fn)
{
    std::lock_guard lock(mutex_);

    // Check for duplicate
    for (const auto& entry : types_)
    {
        if (entry.type_name == type_name)
        {
            SPECTRA_LOG_WARN("series_type_registry",
                             "Series type '{}' already registered",
                             type_name);
            return;
        }
    }

    SeriesTypeEntry entry;
    entry.type_name     = type_name;
    entry.pipeline_desc = pipeline_desc;
    entry.upload_fn     = std::move(upload_fn);
    entry.draw_fn       = std::move(draw_fn);
    entry.bounds_fn     = std::move(bounds_fn);
    entry.cleanup_fn    = std::move(cleanup_fn);

    // Copy SPIR-V bytecode so it persists beyond plugin init.
    if (pipeline_desc.vert_spirv && pipeline_desc.vert_spirv_size > 0)
    {
        entry.vert_spirv_storage.assign(pipeline_desc.vert_spirv,
                                        pipeline_desc.vert_spirv + pipeline_desc.vert_spirv_size);
        entry.pipeline_desc.vert_spirv      = entry.vert_spirv_storage.data();
        entry.pipeline_desc.vert_spirv_size = entry.vert_spirv_storage.size();
    }
    if (pipeline_desc.frag_spirv && pipeline_desc.frag_spirv_size > 0)
    {
        entry.frag_spirv_storage.assign(pipeline_desc.frag_spirv,
                                        pipeline_desc.frag_spirv + pipeline_desc.frag_spirv_size);
        entry.pipeline_desc.frag_spirv      = entry.frag_spirv_storage.data();
        entry.pipeline_desc.frag_spirv_size = entry.frag_spirv_storage.size();
    }

    SPECTRA_LOG_INFO("series_type_registry", "Registered custom series type '{}'", type_name);
    types_.push_back(std::move(entry));
}

void SeriesTypeRegistry::unregister_type(const std::string& name)
{
    std::lock_guard lock(mutex_);

    auto it = std::remove_if(types_.begin(),
                             types_.end(),
                             [&](const SeriesTypeEntry& e) { return e.type_name == name; });
    if (it != types_.end())
    {
        types_.erase(it, types_.end());
        SPECTRA_LOG_INFO("series_type_registry", "Unregistered custom series type '{}'", name);
    }
}

const SeriesTypeEntry* SeriesTypeRegistry::find(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& entry : types_)
    {
        if (entry.type_name == name)
            return &entry;
    }
    return nullptr;
}

SeriesTypeEntry* SeriesTypeRegistry::find_mut(const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto& entry : types_)
    {
        if (entry.type_name == name)
            return &entry;
    }
    return nullptr;
}

void SeriesTypeRegistry::create_pipelines(Backend& backend)
{
    std::lock_guard lock(mutex_);

    for (auto& entry : types_)
    {
        if (entry.pipeline)
        {
            // Already created — skip (call destroy_pipelines first to recreate).
            continue;
        }

        auto& desc = entry.pipeline_desc;
        if (!desc.vert_spirv || desc.vert_spirv_size == 0 || !desc.frag_spirv
            || desc.frag_spirv_size == 0)
        {
            SPECTRA_LOG_WARN("series_type_registry",
                             "Custom series type '{}' has no SPIR-V shaders — skipping pipeline",
                             entry.type_name);
            continue;
        }

        entry.pipeline = backend.create_custom_pipeline(desc);
        if (entry.pipeline)
        {
            SPECTRA_LOG_INFO("series_type_registry",
                             "Created pipeline for custom series type '{}'",
                             entry.type_name);
        }
        else
        {
            SPECTRA_LOG_ERROR("series_type_registry",
                              "Failed to create pipeline for custom series type '{}'",
                              entry.type_name);
        }
    }
}

void SeriesTypeRegistry::destroy_pipelines(Backend& backend)
{
    std::lock_guard lock(mutex_);

    for (auto& entry : types_)
    {
        if (entry.pipeline)
        {
            backend.destroy_pipeline(entry.pipeline);
            entry.pipeline = PipelineHandle{};
        }
    }
}

std::vector<std::string> SeriesTypeRegistry::type_names() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;
    names.reserve(types_.size());
    for (const auto& entry : types_)
    {
        names.push_back(entry.type_name);
    }
    return names;
}

size_t SeriesTypeRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return types_.size();
}

}   // namespace spectra
