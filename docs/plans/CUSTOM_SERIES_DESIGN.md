# Custom Series Type Plugin Interface — Design Document

> **Phase D1** of LT-2 (Plugin API Expansion)
> **Status**: Design complete — ready for D2 implementation
> **Date**: 2026-04-03

---

## 1. Overview

This document defines the minimal interface a custom series type plugin must implement, how custom GPU pipelines are registered, the data upload and draw lifecycle, dirty tracking, which `Backend`/`Renderer` methods must become stable API, ABI stability risks, and two design options (thin wrapper vs. full abstraction) with a recommendation.

### Prerequisites (all met)

| Prerequisite | Status | Notes |
|---|---|---|
| QW-2: Remove `Series::record_commands()` | ✅ Done | Renderer uses type-based dispatch via cached `SeriesType` enum |
| QW-3: Fix `app.hpp` internal include | ✅ Done | Public headers no longer include private headers |
| MR-4: Library split (`spectra-core`) | ✅ Done | Data model decoupled from rendering/Vulkan |
| Phase C1: ExportFormatRegistry | ✅ Done | Plugin context at v1.3 |

---

## 2. Design Constraints

1. **C ABI only** — no STL types cross the plugin boundary.
2. **No per-frame Vulkan resource creation** — pipelines and buffers are created at load time or on data change, never during `render_series()`.
3. **Shared pipeline layout** — custom pipelines must use the same descriptor set layout and push constant layout as built-in pipelines. This ensures plugins share the existing UBO binding (set 0) and SSBO binding (set 1) without requiring new descriptor pool configurations.
4. **SPIR-V only** — plugins provide pre-compiled SPIR-V bytecodes, not GLSL source.
5. **No direct Vulkan access** — plugins interact with the GPU exclusively through the `Backend` C ABI wrappers (`spectra_backend_*` functions).
6. **Deferred GPU deletion** — buffer destruction follows the existing deletion ring pattern (wait `MAX_FRAMES_IN_FLIGHT + 2` frames).
7. **Thread safety** — `SeriesTypeRegistry` is mutex-protected. Registration happens only during `spectra_plugin_init()`.

---

## 3. Plugin Interface

### 3.1 C ABI Descriptor

```c
typedef struct SpectraSeriesTypeDesc
{
    /* ── Identity ── */
    const char* type_name;          /* Unique name, e.g. "heatmap", "candlestick" */
    uint32_t    flags;              /* SpectraSeriesFlags bitmask (see below) */

    /* ── Pipeline ── */
    const uint8_t* vert_spirv;     /* Pre-compiled vertex shader SPIR-V */
    size_t         vert_spirv_size;
    const uint8_t* frag_spirv;     /* Pre-compiled fragment shader SPIR-V */
    size_t         frag_spirv_size;
    uint32_t       topology;       /* VkPrimitiveTopology value (0 = TRIANGLE_LIST) */

    /* Vertex input layout (for pipelines that use vertex attributes).
       NULL / 0 means the pipeline uses SSBOs only (like Line, Scatter). */
    const SpectraVertexBinding*   vertex_bindings;
    uint32_t                      vertex_binding_count;
    const SpectraVertexAttribute* vertex_attributes;
    uint32_t                      vertex_attribute_count;

    /* ── Callbacks ── */

    /* Upload raw data to GPU. Called when the series is dirty.
       Plugin receives: opaque backend handle, opaque series data pointer,
       a mutable pointer to its own GPU state, and the data count.
       Must use spectra_backend_* helpers to create/upload buffers.
       Returns 0 on success. */
    int (*upload_fn)(SpectraBackendHandle backend,
                     const void*         series_data,
                     void*               gpu_state,      /* plugin-managed, persisted */
                     size_t              data_count,
                     void*               user_data);

    /* Record draw commands. Called once per visible series per frame.
       Plugin receives: opaque backend handle, its pipeline handle,
       its GPU state, viewport rect, push constants, and draw helpers.
       Must use spectra_backend_* helpers to bind/draw.
       Returns 0 on success. */
    int (*draw_fn)(SpectraBackendHandle          backend,
                   SpectraPipelineHandle         pipeline,
                   const void*                   gpu_state,
                   const SpectraViewport*        viewport,
                   const SpectraSeriesPushConst* push_constants,
                   void*                         user_data);

    /* Compute axis-aligned bounding box of the series data.
       Used by autoscale. Returns 0 on success. */
    int (*bounds_fn)(const void*  series_data,
                     size_t       data_count,
                     SpectraRect* bounds_out,
                     void*        user_data);

    /* (Optional) Cleanup GPU state when series is removed.
       Called on the render thread. Use spectra_backend_destroy_buffer(). */
    void (*cleanup_fn)(SpectraBackendHandle backend,
                       void*               gpu_state,
                       void*               user_data);

    /* Opaque pointer passed to all callbacks. */
    void* user_data;

} SpectraSeriesTypeDesc;
```

### 3.2 Supporting C ABI Types

```c
/* Matches VkFormat values for common vertex formats */
typedef enum SpectraVertexFormat
{
    SPECTRA_FORMAT_R32_SFLOAT          = 100,  /* VK_FORMAT_R32_SFLOAT */
    SPECTRA_FORMAT_R32G32_SFLOAT       = 103,  /* VK_FORMAT_R32G32_SFLOAT */
    SPECTRA_FORMAT_R32G32B32_SFLOAT    = 106,  /* VK_FORMAT_R32G32B32_SFLOAT */
    SPECTRA_FORMAT_R32G32B32A32_SFLOAT = 109,  /* VK_FORMAT_R32G32B32A32_SFLOAT */
} SpectraVertexFormat;

typedef struct SpectraVertexBinding
{
    uint32_t binding;
    uint32_t stride;
    uint32_t input_rate;  /* 0 = per-vertex, 1 = per-instance */
} SpectraVertexBinding;

typedef struct SpectraVertexAttribute
{
    uint32_t location;
    uint32_t binding;
    uint32_t format;   /* SpectraVertexFormat */
    uint32_t offset;
} SpectraVertexAttribute;

typedef struct SpectraViewport
{
    float x, y, width, height;
} SpectraViewport;

typedef struct SpectraRect
{
    float x_min, x_max, y_min, y_max;
} SpectraRect;

/* Mirrors SeriesPushConstants — stable C ABI version */
typedef struct SpectraSeriesPushConst
{
    float    color[4];
    float    line_width;
    float    point_size;
    float    data_offset_x;
    float    data_offset_y;
    uint32_t line_style;
    uint32_t marker_type;
    float    marker_size;
    float    opacity;
    float    dash_pattern[8];
    float    dash_total;
    int32_t  dash_count;
    float    _pad[2];
} SpectraSeriesPushConst;

/* Series creation flags */
typedef enum SpectraSeriesFlags
{
    SPECTRA_SERIES_FLAG_NONE          = 0,
    SPECTRA_SERIES_FLAG_3D            = (1 << 0),  /* Uses depth test/write */
    SPECTRA_SERIES_FLAG_TRANSPARENT   = (1 << 1),  /* Depth test ON, write OFF */
    SPECTRA_SERIES_FLAG_INDEXED       = (1 << 2),  /* Uses index buffer */
    SPECTRA_SERIES_FLAG_INSTANCED     = (1 << 3),  /* Uses instanced drawing */
    SPECTRA_SERIES_FLAG_BACKFACE_CULL = (1 << 4),  /* Enable backface culling */
} SpectraSeriesFlags;

/* Opaque handles */
typedef struct { uint64_t id; } SpectraBackendHandle;
typedef struct { uint64_t id; } SpectraPipelineHandle;
typedef struct { uint64_t id; } SpectraBufferHandle;
```

### 3.3 Backend C ABI Helpers

Thin wrappers around `Backend` methods, callable from plugin code:

```c
/* Buffer management */
SpectraBufferHandle spectra_backend_create_buffer(SpectraBackendHandle backend,
                                                  uint32_t             usage,   /* BufferUsage enum */
                                                  size_t               size_bytes);
void spectra_backend_destroy_buffer(SpectraBackendHandle backend,
                                    SpectraBufferHandle  buffer);
void spectra_backend_upload_buffer(SpectraBackendHandle backend,
                                   SpectraBufferHandle  buffer,
                                   const void*          data,
                                   size_t               size_bytes,
                                   size_t               offset);

/* Draw commands (only valid inside draw_fn callback) */
void spectra_backend_bind_pipeline(SpectraBackendHandle  backend,
                                   SpectraPipelineHandle pipeline);
void spectra_backend_bind_buffer(SpectraBackendHandle backend,
                                 SpectraBufferHandle  buffer,
                                 uint32_t             binding);
void spectra_backend_bind_index_buffer(SpectraBackendHandle backend,
                                       SpectraBufferHandle  buffer);
void spectra_backend_push_constants(SpectraBackendHandle         backend,
                                    const SpectraSeriesPushConst* pc);
void spectra_backend_draw(SpectraBackendHandle backend,
                          uint32_t             vertex_count,
                          uint32_t             first_vertex);
void spectra_backend_draw_instanced(SpectraBackendHandle backend,
                                    uint32_t             vertex_count,
                                    uint32_t             instance_count,
                                    uint32_t             first_vertex,
                                    uint32_t             first_instance);
void spectra_backend_draw_indexed(SpectraBackendHandle backend,
                                  uint32_t             index_count,
                                  uint32_t             first_index,
                                  int32_t              vertex_offset);
```

---

## 4. Pipeline Registration Lifecycle

### 4.1 When Pipelines Are Created

```
Plugin .so loaded
    └─ spectra_plugin_init() called
        └─ spectra_register_series_type(registry, &desc)
            └─ SeriesTypeRegistry stores desc (no GPU work yet)

Renderer::init() (or Renderer::rebuild_pipelines())
    └─ For each registered custom type:
        1. Build PipelineConfig from SpectraSeriesTypeDesc:
           - vert_spirv, frag_spirv from desc
           - topology from desc
           - vertex_bindings / vertex_attributes from desc
           - enable_depth_test / enable_depth_write from flags
           - enable_backface_cull from flags
           - enable_blending = true (always)
           - pipeline_layout = pipeline_layout_ (shared)
           - render_pass = current render pass
           - msaa_samples = current MSAA level
        2. Call backend.create_pipeline() → PipelineHandle
        3. Store handle in SeriesTypeRegistry entry
```

### 4.2 When Pipelines Are Rebuilt

Pipelines must be recreated when:
- Swapchain is recreated (window resize, format change)
- MSAA sample count changes
- Render pass is recreated

The existing `VulkanBackend::recreate_pipelines()` path already handles built-in pipeline recreation. It will be extended to iterate `SeriesTypeRegistry` and recreate custom pipelines using the stored `SpectraSeriesTypeDesc`.

### 4.3 When Pipelines Are Destroyed

- On plugin unload: `SeriesTypeRegistry::unregister(type_name)` destroys the pipeline handle via `backend.destroy_pipeline()`.
- On application shutdown: all pipelines destroyed in `Renderer::shutdown()`.

---

## 5. Data Upload Lifecycle

### 5.1 Flow

```
Each frame:
    Renderer::render_figure(figure)
        └─ For each axes in figure:
            └─ For each series in axes:
                └─ if (series.is_dirty())
                    └─ Renderer::upload_custom_series(series)
                        1. Look up SeriesType tag → find SpectraSeriesTypeDesc
                        2. Allocate/find the plugin's gpu_state for this series instance
                        3. Call desc.upload_fn(backend_handle, series_data, gpu_state, count, user_data)
                           - Plugin uses spectra_backend_create_buffer() / upload_buffer()
                           - Plugin stores buffer handles in its own gpu_state
                        4. series.clear_dirty()
```

### 5.2 Plugin GPU State

Each `(custom_type, series_instance)` pair gets an opaque `void* gpu_state` block managed by the host:

```cpp
// Host-side, inside Renderer's series_gpu_data_ map:
struct SeriesGpuData {
    // ... existing fields ...
    SeriesType type;

    // For custom series types:
    std::string custom_type_name;          // links to SeriesTypeRegistry entry
    std::vector<uint8_t> plugin_gpu_state; // opaque blob, size declared at registration
    // (alternatively: void* allocated by the plugin in upload_fn, freed in cleanup_fn)
};
```

**Preferred approach**: the plugin allocates its own state in `upload_fn` (first call) and stores it via the `gpu_state` pointer (pointer-to-pointer pattern). The host stores just a `void*` per series. The plugin frees it in `cleanup_fn`.

### 5.3 Dirty Tracking

Custom series use the same `Series::is_dirty()` / `clear_dirty()` mechanism as built-in types:

- Plugin data is set via a new `CustomSeries` subclass (see Section 7) which holds a `void*` data pointer + count.
- When the user calls `custom_series.set_data(ptr, count)`, `dirty_ = true` is set.
- The Renderer checks `is_dirty()` and calls `upload_fn` if true.
- After upload, the Renderer calls `clear_dirty()`.

---

## 6. Draw Command Recording

### 6.1 Flow

```
Renderer::render_figure_content(figure)
    └─ For each axes:
        └─ Set viewport + scissor for axes rect
        └─ Upload frame UBO (projection, view, model)
        └─ For each series in axes:
            └─ Renderer::render_series(series, axes, ...)
                └─ switch (gpu.type):
                    case SeriesType::Custom:
                        auto& entry = series_type_registry_.get(gpu.custom_type_name);
                        // Host sets up viewport and push constants
                        SpectraSeriesPushConst pc = build_push_constants(series);
                        SpectraViewport vp = current_viewport();
                        // Delegate to plugin
                        entry.desc.draw_fn(backend_handle,
                                           entry.pipeline_handle,
                                           gpu.plugin_gpu_state,
                                           &vp, &pc,
                                           entry.desc.user_data);
                    // ... built-in cases unchanged ...
```

### 6.2 What the Host Provides Before `draw_fn`

1. **UBO already bound** at set 0 — projection/view/model matrices are current for this axes.
2. **Viewport and scissor already set** for this axes rectangle.
3. **Push constants pre-populated** with series color, opacity, line width, etc. from the `Series` base class. Plugin's `draw_fn` may modify and re-push if needed.

### 6.3 What the Plugin Does in `draw_fn`

1. Call `spectra_backend_bind_pipeline(backend, pipeline)` — bind its custom pipeline.
2. Call `spectra_backend_bind_buffer(backend, ssbo, 0)` — bind its data SSBO at set 1 binding 0.
3. Optionally modify push constants and call `spectra_backend_push_constants()`.
4. Call `spectra_backend_draw()` / `draw_instanced()` / `draw_indexed()`.

**No buffer creation, no pipeline creation, no descriptor set allocation.**

---

## 7. CustomSeries Data Model

A new `CustomSeries` subclass in `src/core/`:

```cpp
class CustomSeries : public Series
{
public:
    CustomSeries(const std::string& type_name);

    const std::string& type_name() const { return type_name_; }

    // Generic data setter — plugin defines the format
    void set_data(const void* data, size_t byte_size, size_t element_count);
    const void* data() const { return data_.data(); }
    size_t data_byte_size() const { return data_.size(); }
    size_t element_count() const { return element_count_; }

    // Bounds (computed by plugin's bounds_fn)
    void set_bounds(float x_min, float x_max, float y_min, float y_max);

private:
    std::string type_name_;                  // matches SpectraSeriesTypeDesc::type_name
    std::vector<uint8_t> data_;              // raw bytes, format defined by plugin
    size_t element_count_ = 0;
};
```

This class lives in `spectra-core` (no render dependency). The Renderer recognizes it via `SeriesType::Custom` and routes to the plugin callbacks.

---

## 8. Backend / Renderer Methods That Must Become Stable API

### 8.1 Backend Methods (already abstract virtual — stable)

All required Backend methods are already part of the abstract interface and will not change:

| Method | Used by plugins via | Notes |
|---|---|---|
| `create_buffer()` | `spectra_backend_create_buffer()` | Already stable |
| `destroy_buffer()` | `spectra_backend_destroy_buffer()` | Already stable |
| `upload_buffer()` | `spectra_backend_upload_buffer()` | Already stable |
| `bind_pipeline()` | `spectra_backend_bind_pipeline()` | Already stable |
| `bind_buffer()` | `spectra_backend_bind_buffer()` | Already stable |
| `bind_index_buffer()` | `spectra_backend_bind_index_buffer()` | Already stable |
| `push_constants()` | `spectra_backend_push_constants()` | Already stable |
| `draw()` | `spectra_backend_draw()` | Already stable |
| `draw_instanced()` | `spectra_backend_draw_instanced()` | Already stable |
| `draw_indexed()` | `spectra_backend_draw_indexed()` | Already stable |

### 8.2 New Renderer Public Methods

| Method | Purpose |
|---|---|
| `register_custom_series_type()` | Register a plugin series type (delegates to `SeriesTypeRegistry`) |
| `unregister_custom_series_type()` | Remove on plugin unload |
| `create_custom_pipeline()` | Build the `PipelineHandle` from a `SpectraSeriesTypeDesc` |

### 8.3 New Public Types

| Type | Location | Purpose |
|---|---|---|
| `SeriesTypeRegistry` | `src/render/series_type_registry.hpp` | Registry of custom series type descriptors + pipeline handles |
| `CustomSeries` | `include/spectra/custom_series.hpp` | Data model subclass for plugin-defined series |

---

## 9. SPIR-V Shader Requirements for Plugins

Custom shaders MUST conform to the existing descriptor set and push constant layout:

### 9.1 Descriptor Set Layout

```glsl
// Set 0, binding 0: Frame UBO (host-managed, read-only)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  projection;       // Orthographic (2D) or perspective (3D)
    mat4  view;             // Identity (2D) or camera matrix (3D)
    mat4  model;            // Identity (2D) or per-series transform (3D)
    vec2  viewport_size;    // Current viewport in pixels
    float time;             // Animation time in seconds
    float _pad0;
    vec3  camera_pos;       // Eye position (3D)
    float near_plane;
    vec3  light_dir;        // Directional light (3D)
    float far_plane;
};

// Set 1, binding 0: Series data SSBO (plugin-managed via upload_fn)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    // Plugin defines the format (vec2, vec3, custom structs)
    // Must match what upload_fn uploads
};
```

### 9.2 Push Constant Layout

```glsl
layout(push_constant) uniform SeriesPC {
    vec4  color;            // Series color (host-set from Series::color())
    float line_width;
    float point_size;
    float data_offset_x;   // Camera-relative origin (2D)
    float data_offset_y;
    uint  line_style;       // 0=None, 1=Solid, 2=Dashed, ...
    uint  marker_type;      // 0=None, 1=Circle, ...
    float marker_size;
    float opacity;
    float dash_pattern[8];
    float dash_total;
    int   dash_count;
    float _pad[2];
};
// Total: 80 bytes (within Vulkan minPushConstantsSize of 128)
```

Plugins may use any subset of these fields. Unused fields can be repurposed for plugin-specific parameters (e.g., `dash_pattern[0..7]` for custom floats), but the total push constant block size is fixed at 80 bytes.

### 9.3 Vertex Input

Two styles are supported:

1. **SSBO-only** (recommended, like Line/Scatter): No vertex attributes. Data is fetched from SSBO using `gl_VertexIndex` or `gl_InstanceIndex`. This is the most flexible — the host doesn't need to know the vertex format.

2. **Vertex attributes** (like Grid/Surface): Plugin declares `vertex_bindings` and `vertex_attributes` in `SpectraSeriesTypeDesc`. The host creates the pipeline with matching `VkVertexInputState`. The plugin uploads vertex data to a vertex buffer (using `BufferUsage::Vertex`) and binds it in `draw_fn`.

---

## 10. Risk Assessment

### 10.1 ABI Stability of Pipeline Creation Parameters

| Risk | Severity | Mitigation |
|---|---|---|
| Push constant layout changes | High | Freeze at 80 bytes. Future extensions use a second push constant range (requires pipeline layout versioning). |
| UBO layout changes | High | Freeze `FrameUBO` at 112 bytes (current size). Append new fields after padding, protected by `api_version_major`. |
| Descriptor set layout changes | High | Set 0 (UBO) and Set 1 (SSBO) are frozen. New bindings go to Set 2+, requiring a new pipeline layout reserved for plugin pipelines with extended set counts. |
| MSAA sample count mismatch | Medium | Host recreates plugin pipelines on MSAA change automatically (stores desc, not just handle). |
| Render pass compatibility break | Medium | Plugin pipelines are created against the swapchain render pass. On swapchain recreate, all pipelines (built-in + custom) are rebuilt from stored configs. Already handled for built-in pipelines. |
| Buggy SPIR-V causing GPU hang | High | Validate SPIR-V at registration time using `spirv-val` (if available) or basic header checks. Wrap fence waits with timeouts. Enable validation layers in debug builds. |
| Plugin forgets cleanup_fn | Medium | Host tracks all buffer handles created by plugin via backend wrappers. On series removal, if `cleanup_fn` is NULL, host destroys all tracked buffers for that series. |

### 10.2 Performance Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Plugin upload_fn allocates per-frame | Medium | Document contract: upload_fn is called only when dirty. Host enforces by only calling when `is_dirty()` is true. |
| Plugin draw_fn is slow | Low | No different from built-in series — same frame budget. Document expectations. |
| Too many custom pipelines | Low | Pipeline creation is O(1) per type. Practical limit: ~50 types before descriptor pool exhaustion. Sufficient for any realistic use case. |

---

## 11. Two Design Options

### Option A: Thin Wrapper (Recommended)

**Concept**: Plugins get direct (but C ABI-wrapped) access to `Backend` operations. The host manages pipeline creation, UBO binding, viewport/scissor setup, and dirty tracking. Plugins implement `upload_fn` and `draw_fn` using `spectra_backend_*` helpers.

**Pros**:
- Maximum flexibility — plugins can implement any rendering technique that Vulkan supports within the pipeline constraints.
- Minimal host-side abstraction layer — easier to maintain.
- Matches how built-in series types already work (Renderer dispatches to type-specific code that calls Backend methods directly).
- No performance overhead beyond the C ABI function call indirection.

**Cons**:
- Plugins must understand the SSBO/vertex attribute model and Vulkan draw call semantics.
- More room for plugin bugs (wrong vertex count, missing bind calls).
- Plugin developers need to write SPIR-V compatible shaders.

**Implementation cost**: ~300 lines in `SeriesTypeRegistry` + ~200 lines of C ABI wrappers + ~100 lines in Renderer dispatch.

### Option B: Full Abstraction

**Concept**: Define a high-level `CustomSeriesRenderer` abstraction that hides all GPU details. Plugins declare a data schema (e.g., "2 floats per point, triangle topology") and provide only data + a coloring function. The host generates the upload and draw logic automatically.

```c
// Plugin just provides data layout and data
typedef struct SpectraSeriesSchema {
    const char* type_name;
    uint32_t    floats_per_element;   // e.g., 2 for 2D, 3 for 3D
    uint32_t    topology;             // TRIANGLE_LIST, LINE_LIST, etc.
    uint32_t    builtin_shader;       // 0=line, 1=scatter, 2=fill — reuse existing shaders
} SpectraSeriesSchema;

// Upload is automatic — host interleaves data like built-in types
// Draw is automatic — host selects pipeline based on schema
```

**Pros**:
- Very simple for plugin developers — no GPU knowledge needed.
- Host controls all GPU operations — safer, fewer bugs.
- No SPIR-V required from plugins (reuse built-in shaders).

**Cons**:
- Severely limited — can only render things that look like existing series types.
- Cannot implement novel visualizations (heatmaps, candlestick charts, vector fields, contour plots).
- Adds a significant abstraction layer that built-in types don't use — maintenance burden.
- Defeats the purpose of custom series types — if you want a line or scatter plot, use the built-in ones.

**Implementation cost**: ~500 lines of schema interpretation + ~400 lines of auto-generated upload/draw logic. Higher ongoing maintenance.

### Recommendation

**Option A (Thin Wrapper)** is recommended. The primary value of custom series types is enabling novel visualizations that cannot be built from existing primitives. Option B would restrict plugins to variations of existing series types, which is already achievable through overlays and transforms.

Option A aligns with the existing Renderer architecture (type-dispatched Backend calls) and adds minimal abstraction. The `spectra_backend_*` C ABI wrappers provide safety without limiting capability.

A future convenience layer (built on top of Option A) could provide schema-based shortcuts for common patterns, but the core plugin interface should remain the thin wrapper.

---

## 12. Example: Heatmap Plugin (Option A)

```c
/* heatmap_plugin.c — Example custom series type plugin */

#include <spectra/plugin_api.h>
#include <string.h>

/* Embedded SPIR-V (compiled from heatmap.vert / heatmap.frag) */
extern const uint8_t heatmap_vert_spirv[];
extern size_t        heatmap_vert_spirv_size;
extern const uint8_t heatmap_frag_spirv[];
extern size_t        heatmap_frag_spirv_size;

/* Per-series GPU state managed by this plugin */
typedef struct HeatmapGpuState {
    SpectraBufferHandle ssbo;
    size_t              uploaded_count;
    uint32_t            grid_width;
    uint32_t            grid_height;
} HeatmapGpuState;

static int heatmap_upload(SpectraBackendHandle backend,
                          const void*          series_data,
                          void*                gpu_state_ptr,
                          size_t               data_count,
                          void*                user_data)
{
    HeatmapGpuState** state_pp = (HeatmapGpuState**)gpu_state_ptr;
    const float* data = (const float*)series_data;

    /* First call — allocate state */
    if (*state_pp == NULL) {
        *state_pp = calloc(1, sizeof(HeatmapGpuState));
    }
    HeatmapGpuState* state = *state_pp;

    /* Reallocate buffer if needed */
    size_t byte_size = data_count * sizeof(float);
    if (state->ssbo.id == 0 || state->uploaded_count < data_count) {
        if (state->ssbo.id != 0)
            spectra_backend_destroy_buffer(backend, state->ssbo);
        state->ssbo = spectra_backend_create_buffer(backend, 3 /* Storage */, byte_size * 2);
    }

    spectra_backend_upload_buffer(backend, state->ssbo, data, byte_size, 0);
    state->uploaded_count = data_count;
    return 0;
}

static int heatmap_draw(SpectraBackendHandle          backend,
                        SpectraPipelineHandle         pipeline,
                        const void*                   gpu_state_ptr,
                        const SpectraViewport*        viewport,
                        const SpectraSeriesPushConst* pc,
                        void*                         user_data)
{
    const HeatmapGpuState* state = *(const HeatmapGpuState* const*)gpu_state_ptr;
    if (!state || state->ssbo.id == 0) return 0;

    spectra_backend_bind_pipeline(backend, pipeline);
    spectra_backend_bind_buffer(backend, state->ssbo, 0);

    /* Encode grid dimensions in push constants */
    SpectraSeriesPushConst custom_pc = *pc;
    custom_pc.dash_pattern[0] = (float)state->grid_width;
    custom_pc.dash_pattern[1] = (float)state->grid_height;
    spectra_backend_push_constants(backend, &custom_pc);

    /* 6 vertices per cell (2 triangles) */
    uint32_t cell_count = state->grid_width * state->grid_height;
    spectra_backend_draw(backend, cell_count * 6, 0);
    return 0;
}

static int heatmap_bounds(const void*  series_data,
                          size_t       data_count,
                          SpectraRect* bounds_out,
                          void*        user_data)
{
    /* Heatmap covers [0, grid_width] x [0, grid_height] */
    bounds_out->x_min = 0.0f;
    bounds_out->x_max = 10.0f;  /* real implementation reads from data */
    bounds_out->y_min = 0.0f;
    bounds_out->y_max = 10.0f;
    return 0;
}

static void heatmap_cleanup(SpectraBackendHandle backend,
                            void*               gpu_state_ptr,
                            void*               user_data)
{
    HeatmapGpuState** state_pp = (HeatmapGpuState**)gpu_state_ptr;
    if (*state_pp) {
        if ((*state_pp)->ssbo.id != 0)
            spectra_backend_destroy_buffer(backend, (*state_pp)->ssbo);
        free(*state_pp);
        *state_pp = NULL;
    }
}

int spectra_plugin_init(const SpectraPluginContext* ctx,
                        SpectraPluginInfo*         info_out)
{
    info_out->name              = "Heatmap Series";
    info_out->version           = "1.0.0";
    info_out->author            = "Example Author";
    info_out->description       = "Adds a heatmap series type";
    info_out->api_version_major = 2;
    info_out->api_version_minor = 0;

    SpectraSeriesTypeDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type_name       = "heatmap";
    desc.flags           = SPECTRA_SERIES_FLAG_NONE;
    desc.vert_spirv      = heatmap_vert_spirv;
    desc.vert_spirv_size = heatmap_vert_spirv_size;
    desc.frag_spirv      = heatmap_frag_spirv;
    desc.frag_spirv_size = heatmap_frag_spirv_size;
    desc.topology        = 3; /* VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST */
    desc.upload_fn       = heatmap_upload;
    desc.draw_fn         = heatmap_draw;
    desc.bounds_fn       = heatmap_bounds;
    desc.cleanup_fn      = heatmap_cleanup;

    spectra_register_series_type(ctx->series_type_registry, &desc);
    return 0;
}

void spectra_plugin_shutdown(void)
{
    /* Nothing to clean up at plugin level */
}
```

---

## 13. Implementation Plan (D2 / D3)

### D2 Scope

1. **`src/render/series_type_registry.hpp/.cpp`** — `SeriesTypeRegistry` class:
   - `register_type(SpectraSeriesTypeDesc)` — stores desc, assigns `SeriesType::Custom` tag
   - `unregister_type(name)` — removes entry, schedules pipeline destruction
   - `find(name) → entry*` — lookup for render dispatch
   - `create_pipelines(Backend&, render_pass)` — build all custom pipelines
   - `destroy_pipelines(Backend&)` — cleanup
   - Mutex-protected for thread safety

2. **`include/spectra/custom_series.hpp`** — `CustomSeries` class (in `spectra-core`)

3. **`src/render/renderer.hpp`** — Add `SeriesTypeRegistry*` member, `SeriesType::Custom` case in dispatch

4. **`src/render/render_upload.cpp`** — Add custom series upload path

5. **`src/render/render_2d.cpp` and `render_3d.cpp`** — Add `SeriesType::Custom` case in render dispatch, calling plugin's `draw_fn`

6. **`src/ui/workspace/plugin_api.hpp/.cpp`** — Add `SpectraSeriesTypeRegistry` handle to context, `spectra_register_series_type()`, `spectra_backend_*` wrappers, bump API version to 2.0

7. **`src/render/vulkan/vk_backend.cpp`** — Extend `recreate_pipelines()` to include custom pipelines

### D3 Scope

1. **`tests/unit/test_plugin_series_type.cpp`** — Unit test: register custom type, create figure, add `CustomSeries`, render one frame, verify no validation errors
2. **`examples/plugins/series_heatmap.cpp`** — Example plugin with custom shaders
3. **Resize torture test** — verify custom pipelines survive swapchain recreation
4. **Validation** — ASAN clean, no GPU validation errors

---

## 14. Open Questions

| # | Question | Proposed Answer |
|---|---|---|
| 1 | Should custom series support transparency sorting? | No — use `SPECTRA_SERIES_FLAG_TRANSPARENT` to disable depth writes. Full OIT is out of scope. |
| 2 | Can a single plugin register multiple series types? | Yes — call `spectra_register_series_type()` multiple times. |
| 3 | Can custom series participate in selection/highlight? | Yes — the host passes selection state via push constants (same as built-in). |
| 4 | Should plugins be able to add new push constant fields? | No — fixed at 80 bytes. Use the `dash_pattern[8]` and `_pad[2]` fields for custom floats (document this convention). |
| 5 | Should custom series work in both 2D and 3D axes? | Yes — the `SPECTRA_SERIES_FLAG_3D` flag determines depth test/write behavior. The plugin shader decides whether to use the 3D `view`/`model` matrices. |
| 6 | How does the host know the data format for autoscale? | Via `bounds_fn` callback. The host calls it and uses the returned `SpectraRect` for axis limit computation. |
| 7 | What happens if a plugin's draw_fn crashes? | Same as any series crash — undefined behavior. Debug builds have validation layers + fence timeouts. Release builds: plugin at own risk (documented). |
