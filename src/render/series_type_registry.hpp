#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "backend.hpp"
#include "io/vector_sink.hpp"

namespace spectra
{

class CustomSeries;

// Callback signatures for custom series type plugins (C++ wrappers).
//
// gpu_state is the host-owned per-series slot (initially nullptr).  upload_fn
// and cleanup_fn receive it by reference so the plugin can allocate its state
// on the first upload and release it on cleanup; draw_fn receives its value.
using CustomUploadFn = std::function<
    int(Backend& backend, const void* series_data, void*& gpu_state, size_t data_count)>;
using CustomDrawFn    = std::function<int(Backend&                   backend,
                                       PipelineHandle             pipeline,
                                       const void*                gpu_state,
                                       const float*               viewport_xywh,
                                       const SeriesPushConstants& push_constants)>;
using CustomBoundsFn  = std::function<int(const void* series_data,
                                         size_t      data_count,
                                         float*      bounds_out)>;   // [xmin, xmax, ymin, ymax]
using CustomCleanupFn = std::function<void(Backend& backend, void*& gpu_state)>;

/// CPU export of a custom series: `series_rgba` is the series colour as drawn
/// (alpha includes the opacity), `opacity` the series opacity.  0 = success.
using CustomVectorExportFn = std::function<int(const void*       series_data,
                                               size_t            data_count,
                                               const float*      series_rgba,
                                               float             opacity,
                                               CustomVectorSink& sink)>;

/// An entry in the series type registry (internal storage).
struct SeriesTypeEntry
{
    std::string        type_name;
    CustomPipelineDesc pipeline_desc;
    PipelineHandle     pipeline;   // Set by create_pipelines()
    CustomUploadFn     upload_fn;
    CustomDrawFn       draw_fn;
    CustomBoundsFn     bounds_fn;
    CustomCleanupFn    cleanup_fn;   // May be empty

    // Optional CPU geometry for vector (SVG) export; empty = not exportable.
    // Set on the UI thread (plugin init) and read there by the exporter.
    CustomVectorExportFn vector_export_fn;

    // Persistent copy of SPIR-V bytecode (the registration pointer may not outlive the plugin init)
    std::vector<uint8_t> vert_spirv_storage;
    std::vector<uint8_t> frag_spirv_storage;

    // Persistent copies of the vertex input layout (same lifetime reason as SPIR-V).
    std::vector<CustomPipelineDesc::VertexBinding>   vertex_binding_storage;
    std::vector<CustomPipelineDesc::VertexAttribute> vertex_attribute_storage;

    bool faulted         = false;   // Set true on crash/exception to skip future invocations
    bool pipeline_failed = false;   // Pipeline creation failed — don't retry every frame

    // Opaque owner token (the plugin module that registered the type); nullptr
    // for the host.  See SeriesTypeRegistry::unregister_owner().
    const void* owner = nullptr;

    // False while the owning plugin is disabled: the Renderer neither uploads nor
    // draws series of this type (cleanup_fn still runs when a series is removed).
    // Written on the UI thread, read on the render thread.
    std::atomic<bool> enabled{true};
};

/// Registry for plugin-defined custom series types.
/// Thread-safe for registration; pipeline creation happens on the render thread.
class SeriesTypeRegistry
{
   public:
    SeriesTypeRegistry()  = default;
    ~SeriesTypeRegistry() = default;

    SeriesTypeRegistry(const SeriesTypeRegistry&)            = delete;
    SeriesTypeRegistry& operator=(const SeriesTypeRegistry&) = delete;

    /// Register a custom series type.  Stores the descriptor and callbacks.
    /// The pipeline is not created until create_pipelines() is called.
    void register_type(const std::string&        type_name,
                       const CustomPipelineDesc& pipeline_desc,
                       CustomUploadFn            upload_fn,
                       CustomDrawFn              draw_fn,
                       CustomBoundsFn            bounds_fn,
                       CustomCleanupFn           cleanup_fn = {},
                       const void*               owner      = nullptr);

    /// Remove a custom series type by name.  Unregister listeners are invoked
    /// first (with the lock released and the entry still registered) so the
    /// Renderer can run the plugin's cleanup_fn for live series while the plugin
    /// library is still loaded.  The pipeline (if any) is then moved to the
    /// retired list; the render thread destroys it once no in-flight frame can
    /// reference it (see take_retired_pipelines()).
    /// Must be called on the render thread when a Renderer is attached.
    void unregister_type(const std::string& name);

    /// Same as unregister_type(), but removes exactly this entry: a no-op (returns
    /// false) if it is no longer registered, even when another entry with the
    /// same name is.
    bool unregister_entry(const std::shared_ptr<SeriesTypeEntry>& entry);

    /// unregister_entry() for every type registered with `owner` (nullptr:
    /// none).  Returns the number removed.
    size_t unregister_owner(const void* owner);

    /// Enable/disable every type registered with `owner` (see
    /// SeriesTypeEntry::enabled).  Disabled types stay registered and listed.
    void set_owner_enabled(const void* owner, bool enabled);

    /// Number of types registered with `owner`.
    size_t count_owned(const void* owner) const;

    /// Listener invoked by unregister_type() just before an entry is removed.
    using UnregisterListener = std::function<void(SeriesTypeEntry& entry)>;

    /// Register a listener; returns an id for remove_unregister_listener().
    uint64_t add_unregister_listener(UnregisterListener listener);
    void     remove_unregister_listener(uint64_t id);

    /// Hand over pipelines of unregistered types for deferred destruction.
    std::vector<PipelineHandle> take_retired_pipelines();

    /// Attach a vector export callback to the registered type `type_name`.
    /// False if no such type is registered.
    bool set_vector_export(const std::string& type_name, CustomVectorExportFn fn);

    /// Vector (SVG) export of plugin series `series` through its type's
    /// vector_export_fn, guarded like the GPU callbacks.  Returns nullptr when the
    /// series was exported, otherwise why it was not (a static string).
    const char* export_vector(const CustomSeries& series,
                              const float*        series_rgba,
                              float               opacity,
                              CustomVectorSink&   sink);

    /// Look up an entry by name.  Returns nullptr if not found.
    const SeriesTypeEntry* find(const std::string& name) const;

    /// Mutable lookup (needed by Renderer for pipeline handle assignment).
    /// Entries are heap-allocated: the pointer stays valid across later
    /// register_type() calls and is invalidated only by unregister_type().
    SeriesTypeEntry* find_mut(const std::string& name);

    /// Owning lookup: keeps the entry (and its callbacks) alive while in use,
    /// even if the type is unregistered concurrently.  nullptr if not found.
    std::shared_ptr<SeriesTypeEntry> find_shared(const std::string& name);

    /// Create GPU pipelines for all registered custom types that do not have one
    /// yet.  Called from Renderer::init() and lazily from the render thread when
    /// a custom series is drawn whose type was registered after init.
    /// Types whose creation failed are not retried until destroy_pipelines().
    void create_pipelines(Backend& backend);

    /// Destroy all custom pipelines (including retired ones).  Called on shutdown.
    /// The caller must ensure the GPU is idle.
    void destroy_pipelines(Backend& backend);

    /// List all registered type names.
    std::vector<std::string> type_names() const;

    /// Number of registered custom types.
    size_t count() const;

   private:
    mutable std::mutex                                   mutex_;
    std::vector<std::shared_ptr<SeriesTypeEntry>>        types_;
    std::vector<PipelineHandle>                          retired_pipelines_;
    std::vector<std::pair<uint64_t, UnregisterListener>> unregister_listeners_;
    uint64_t                                             next_listener_id_ = 1;
};

}   // namespace spectra
