#include "series_type_registry.hpp"

#include <algorithm>

#include "spectra/logger.hpp"

namespace spectra
{

namespace
{

// Point the descriptor at the entry's owned storage.  Done whenever the
// descriptor is used so a moved/copied entry never references stale memory.
void bind_owned_desc_storage(SeriesTypeEntry& entry)
{
    auto& d = entry.pipeline_desc;
    if (!entry.vert_spirv_storage.empty())
    {
        d.vert_spirv      = entry.vert_spirv_storage.data();
        d.vert_spirv_size = entry.vert_spirv_storage.size();
    }
    if (!entry.frag_spirv_storage.empty())
    {
        d.frag_spirv      = entry.frag_spirv_storage.data();
        d.frag_spirv_size = entry.frag_spirv_storage.size();
    }
    d.vertex_bindings =
        entry.vertex_binding_storage.empty() ? nullptr : entry.vertex_binding_storage.data();
    d.vertex_binding_count = static_cast<uint32_t>(entry.vertex_binding_storage.size());
    d.vertex_attributes =
        entry.vertex_attribute_storage.empty() ? nullptr : entry.vertex_attribute_storage.data();
    d.vertex_attribute_count = static_cast<uint32_t>(entry.vertex_attribute_storage.size());
}

}   // namespace

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
        if (entry->type_name == type_name)
        {
            SPECTRA_LOG_WARN("series_type_registry",
                             "Series type '{}' already registered",
                             type_name);
            return;
        }
    }

    // Heap-allocated so pointers/references handed out by find()/find_mut()/
    // find_shared() stay valid when other types are registered.
    auto  entry_ptr     = std::make_shared<SeriesTypeEntry>();
    auto& entry         = *entry_ptr;
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
    }
    if (pipeline_desc.frag_spirv && pipeline_desc.frag_spirv_size > 0)
    {
        entry.frag_spirv_storage.assign(pipeline_desc.frag_spirv,
                                        pipeline_desc.frag_spirv + pipeline_desc.frag_spirv_size);
    }
    // Copy the vertex input layout too: the plugin typically passes pointers to
    // stack arrays in its init function, which are dead by the time the
    // pipeline is created.
    if (pipeline_desc.vertex_bindings && pipeline_desc.vertex_binding_count > 0)
    {
        entry.vertex_binding_storage.assign(
            pipeline_desc.vertex_bindings,
            pipeline_desc.vertex_bindings + pipeline_desc.vertex_binding_count);
    }
    if (pipeline_desc.vertex_attributes && pipeline_desc.vertex_attribute_count > 0)
    {
        entry.vertex_attribute_storage.assign(
            pipeline_desc.vertex_attributes,
            pipeline_desc.vertex_attributes + pipeline_desc.vertex_attribute_count);
    }
    bind_owned_desc_storage(entry);

    SPECTRA_LOG_INFO("series_type_registry", "Registered custom series type '{}'", type_name);
    types_.push_back(std::move(entry_ptr));
}

void SeriesTypeRegistry::unregister_type(const std::string& name)
{
    unregister_entry(find_shared(name));
}

bool SeriesTypeRegistry::unregister_entry(const std::shared_ptr<SeriesTypeEntry>& entry)
{
    if (!entry)
        return false;

    std::vector<UnregisterListener> listeners;
    {
        std::lock_guard lock(mutex_);
        if (std::find(types_.begin(), types_.end(), entry) == types_.end())
            return false;   // Already unregistered (or a different, same-named entry)
        listeners.reserve(unregister_listeners_.size());
        for (const auto& [id, fn] : unregister_listeners_)
            listeners.push_back(fn);
    }

    // Let listeners (the Renderer) release per-series plugin state while the
    // entry — and therefore the plugin's cleanup_fn — is still registered.
    // Called without the lock held: listeners invoke plugin callbacks, which
    // may call back into the registry.
    for (auto& fn : listeners)
        fn(*entry);

    std::lock_guard lock(mutex_);
    // Remove exactly this entry (by identity), so a same-named type registered
    // by another plugin is never affected.
    auto it = std::find(types_.begin(), types_.end(), entry);
    if (it == types_.end())
        return false;
    if (entry->pipeline)
        retired_pipelines_.push_back(entry->pipeline);
    types_.erase(it);
    SPECTRA_LOG_INFO("series_type_registry",
                     "Unregistered custom series type '{}'",
                     entry->type_name);
    return true;
}

uint64_t SeriesTypeRegistry::add_unregister_listener(UnregisterListener listener)
{
    std::lock_guard lock(mutex_);
    const uint64_t  id = next_listener_id_++;
    unregister_listeners_.emplace_back(id, std::move(listener));
    return id;
}

void SeriesTypeRegistry::remove_unregister_listener(uint64_t id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(unregister_listeners_, [id](const auto& l) { return l.first == id; });
}

std::vector<PipelineHandle> SeriesTypeRegistry::take_retired_pipelines()
{
    std::lock_guard             lock(mutex_);
    std::vector<PipelineHandle> out;
    out.swap(retired_pipelines_);
    return out;
}

const SeriesTypeEntry* SeriesTypeRegistry::find(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& entry : types_)
    {
        if (entry->type_name == name)
            return entry.get();
    }
    return nullptr;
}

SeriesTypeEntry* SeriesTypeRegistry::find_mut(const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto& entry : types_)
    {
        if (entry->type_name == name)
            return entry.get();
    }
    return nullptr;
}

std::shared_ptr<SeriesTypeEntry> SeriesTypeRegistry::find_shared(const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto& entry : types_)
    {
        if (entry->type_name == name)
            return entry;
    }
    return nullptr;
}

void SeriesTypeRegistry::create_pipelines(Backend& backend)
{
    std::lock_guard lock(mutex_);

    for (auto& entry_ptr : types_)
    {
        auto& entry = *entry_ptr;
        if (entry.pipeline || entry.pipeline_failed)
        {
            // Already created (or failed) — skip (call destroy_pipelines first to retry).
            continue;
        }

        bind_owned_desc_storage(entry);
        auto& desc = entry.pipeline_desc;
        if (!desc.vert_spirv || desc.vert_spirv_size == 0 || !desc.frag_spirv
            || desc.frag_spirv_size == 0)
        {
            SPECTRA_LOG_WARN("series_type_registry",
                             "Custom series type '{}' has no SPIR-V shaders — skipping pipeline",
                             entry.type_name);
            entry.pipeline_failed = true;
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
            entry.pipeline_failed = true;
        }
    }
}

void SeriesTypeRegistry::destroy_pipelines(Backend& backend)
{
    std::lock_guard lock(mutex_);

    for (auto& entry_ptr : types_)
    {
        auto& entry = *entry_ptr;
        if (entry.pipeline)
        {
            backend.destroy_pipeline(entry.pipeline);
            entry.pipeline = PipelineHandle{};
        }
        entry.pipeline_failed = false;
    }
    for (auto& pipeline : retired_pipelines_)
        backend.destroy_pipeline(pipeline);
    retired_pipelines_.clear();
}

std::vector<std::string> SeriesTypeRegistry::type_names() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;
    names.reserve(types_.size());
    for (const auto& entry : types_)
    {
        names.push_back(entry->type_name);
    }
    return names;
}

size_t SeriesTypeRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return types_.size();
}

}   // namespace spectra
