#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

// ─── Transform types ────────────────────────────────────────────────────────

enum class TransformType
{
    Identity,        // No-op passthrough
    Log10,           // log10(x), skips non-positive values
    Ln,              // ln(x), skips non-positive values
    Abs,             // |x|
    Negate,          // -x
    Normalize,       // Scale to [0, 1] range
    Standardize,     // (x - mean) / stddev (z-score)
    Derivative,      // dy/dx (finite differences)
    CumulativeSum,   // Running sum
    Diff,            // First difference: y[i] - y[i-1]
    Scale,           // Multiply by a constant
    Offset,          // Add a constant
    Clamp,           // Clamp to [min, max]
    FFT,             // Left-sided FFT magnitude spectrum (frequency domain)
    Custom,          // User-provided function
};

// ─── Transform parameters ───────────────────────────────────────────────────

struct TransformParams
{
    float scale_factor    = 1.0f;    // For Scale transform
    float offset_value    = 0.0f;    // For Offset transform
    float clamp_min       = 0.0f;    // For Clamp transform
    float clamp_max       = 1.0f;    // For Clamp transform
    float log_base        = 10.0f;   // For custom log base (unused by Log10/Ln)
    bool  skip_nan        = true;    // Skip NaN values in output
    bool  fft_db          = false;   // For FFT: output in dB (20*log10(mag))
    float fft_sample_rate = 0.0f;    // For FFT: sample rate for frequency axis
};

// ─── Single transform step ──────────────────────────────────────────────────

// A single transform operation that can be applied to a data vector.
class DataTransform
{
   public:
    using CustomFunc   = std::function<float(float)>;
    using CustomXYFunc = std::function<void(std::span<const float> x_in,
                                            std::span<const float> y_in,
                                            std::vector<float>&    x_out,
                                            std::vector<float>&    y_out)>;

    // Construct a built-in transform
    explicit DataTransform(TransformType          type   = TransformType::Identity,
                           const TransformParams& params = {});

    // Construct a custom per-element transform
    DataTransform(const std::string& name, CustomFunc func);

    // Construct a custom X-Y transform (can change both x and y, and length)
    DataTransform(const std::string& name, CustomXYFunc func);

    // Apply this transform to Y data only (X passes through unchanged).
    // Returns transformed Y values. Output may be shorter than input
    // for transforms like Derivative or Diff.
    void apply_y(std::span<const float> x_in,
                 std::span<const float> y_in,
                 std::vector<float>&    x_out,
                 std::vector<float>&    y_out) const;

    // Apply to a single value (for per-element transforms only).
    // Returns NaN for transforms that require the full array.
    float apply_scalar(float value) const;

    // Metadata
    TransformType          type() const { return type_; }
    const std::string&     name() const { return name_; }
    const TransformParams& params() const { return params_; }
    TransformParams&       params_mut() { return params_; }

    // Whether this transform can be applied per-element (vs needing full array)
    bool is_elementwise() const;

    // Whether this transform changes the length of the data
    bool changes_length() const;

    // Human-readable description
    std::string description() const;

    // Whether this transform can run.  Built-in and host transforms always can.
    // A custom transform registered by a plugin shares one state with every copy
    // of it (registry entry, get_transform() results, pipeline steps, saved
    // pipelines): it is unavailable while its plugin is disabled, and for good
    // once the plugin unloads or the registration is removed or replaced.  An
    // unavailable transform never calls its callback: apply_y() passes the data
    // through unchanged (identity) and apply_scalar() returns the input.
    bool is_available() const;

   private:
    friend class TransformRegistry;

    // Callbacks of a custom transform, shared by all copies so that disabling
    // or unloading the owner reaches every retained copy.
    struct CustomState
    {
        CustomFunc                func;
        CustomXYFunc              xy_func;
        std::atomic<bool>         enabled{true};
        std::atomic<bool>         alive{true};
        mutable std::shared_mutex mutex;   // shared: apply; unique: revoke
    };

    enum class CustomKind : std::uint8_t
    {
        None,
        Scalar,
        XY,
    };

    TransformType                type_;
    std::string                  name_;
    TransformParams              params_;
    CustomKind                   custom_kind_ = CustomKind::None;
    std::shared_ptr<CustomState> custom_;

    // Wraps a state already holding its callback (see TransformRegistry).
    DataTransform(const std::string& name, std::shared_ptr<CustomState> state, CustomKind kind);

    // Registry-side controls of the shared custom state (no-ops otherwise).
    void set_custom_enabled(bool enabled) const;
    // Drops the callbacks for good, waiting for in-flight calls to finish.
    void revoke_custom() const;
    // Whether both transforms are copies of the same custom registration.
    bool same_custom(const DataTransform& other) const
    {
        return custom_ != nullptr && custom_ == other.custom_;
    }

    // Built-in transform implementations
    void apply_identity(std::span<const float> x_in,
                        std::span<const float> y_in,
                        std::vector<float>&    x_out,
                        std::vector<float>&    y_out) const;
    void apply_log10(std::span<const float> x_in,
                     std::span<const float> y_in,
                     std::vector<float>&    x_out,
                     std::vector<float>&    y_out) const;
    void apply_ln(std::span<const float> x_in,
                  std::span<const float> y_in,
                  std::vector<float>&    x_out,
                  std::vector<float>&    y_out) const;
    void apply_abs(std::span<const float> x_in,
                   std::span<const float> y_in,
                   std::vector<float>&    x_out,
                   std::vector<float>&    y_out) const;
    void apply_negate(std::span<const float> x_in,
                      std::span<const float> y_in,
                      std::vector<float>&    x_out,
                      std::vector<float>&    y_out) const;
    void apply_normalize(std::span<const float> x_in,
                         std::span<const float> y_in,
                         std::vector<float>&    x_out,
                         std::vector<float>&    y_out) const;
    void apply_standardize(std::span<const float> x_in,
                           std::span<const float> y_in,
                           std::vector<float>&    x_out,
                           std::vector<float>&    y_out) const;
    void apply_derivative(std::span<const float> x_in,
                          std::span<const float> y_in,
                          std::vector<float>&    x_out,
                          std::vector<float>&    y_out) const;
    void apply_cumulative_sum(std::span<const float> x_in,
                              std::span<const float> y_in,
                              std::vector<float>&    x_out,
                              std::vector<float>&    y_out) const;
    void apply_diff(std::span<const float> x_in,
                    std::span<const float> y_in,
                    std::vector<float>&    x_out,
                    std::vector<float>&    y_out) const;
    void apply_scale(std::span<const float> x_in,
                     std::span<const float> y_in,
                     std::vector<float>&    x_out,
                     std::vector<float>&    y_out) const;
    void apply_offset(std::span<const float> x_in,
                      std::span<const float> y_in,
                      std::vector<float>&    x_out,
                      std::vector<float>&    y_out) const;
    void apply_clamp(std::span<const float> x_in,
                     std::span<const float> y_in,
                     std::vector<float>&    x_out,
                     std::vector<float>&    y_out) const;
    void apply_fft(std::span<const float> x_in,
                   std::span<const float> y_in,
                   std::vector<float>&    x_out,
                   std::vector<float>&    y_out) const;
};

// ─── Transform pipeline ─────────────────────────────────────────────────────

// A chain of transforms applied in sequence.
// Each step's output becomes the next step's input.
class TransformPipeline
{
   public:
    TransformPipeline() = default;
    explicit TransformPipeline(const std::string& name) : name_(name) {}

    // Add a transform step to the end of the pipeline
    void push_back(const DataTransform& transform);
    void push_back(DataTransform&& transform);

    // Insert a transform at a specific position
    void insert(size_t index, const DataTransform& transform);

    // Remove a transform step by index
    void remove(size_t index);

    // Clear all steps
    void clear();

    // Move a step from one position to another
    void move_step(size_t from, size_t to);

    // Enable/disable a step (disabled steps are skipped)
    void set_enabled(size_t index, bool enabled);
    bool is_enabled(size_t index) const;

    // Apply the full pipeline to input data
    void apply(std::span<const float> x_in,
               std::span<const float> y_in,
               std::vector<float>&    x_out,
               std::vector<float>&    y_out) const;

    // Access steps
    size_t               step_count() const { return steps_.size(); }
    const DataTransform& step(size_t index) const { return steps_[index].transform; }
    DataTransform&       step_mut(size_t index) { return steps_[index].transform; }

    // Pipeline metadata
    const std::string& name() const { return name_; }
    void               set_name(const std::string& n) { name_ = n; }

    // Human-readable description of the full pipeline
    std::string description() const;

    // Check if pipeline is empty or all steps disabled (or unavailable)
    bool is_identity() const;

    // Whether every enabled step is available (see DataTransform::is_available).
    // Unavailable steps apply as identity.
    bool is_available() const;

   private:
    struct Step
    {
        DataTransform transform;
        bool          enabled = true;
    };

    std::string       name_;
    std::vector<Step> steps_;
};

// ─── Transform registry ─────────────────────────────────────────────────────

// Registry of available transforms and saved pipelines.
// Thread-safe via internal mutex.
class TransformRegistry
{
   public:
    TransformRegistry();

    // Get the singleton instance
    static TransformRegistry& instance();

    // Register a named custom transform.  `owner` is an opaque token (the
    // plugin module that registered it, nullptr for the host) used by
    // unregister_owner() / set_owner_enabled().
    void register_transform(const std::string&        name,
                            DataTransform::CustomFunc func,
                            const std::string&        description = "",
                            const void*               owner       = nullptr);

    // Register a named custom XY transform
    void register_xy_transform(const std::string&          name,
                               DataTransform::CustomXYFunc func,
                               const std::string&          description = "",
                               const void*                 owner       = nullptr);

    // Remove a custom transform by name.  A plugin-owned transform (non-null
    // owner) is also revoked in every retained copy (see
    // DataTransform::is_available); host transforms keep working in copies.
    // Registering a new transform under the name of a plugin-owned one revokes
    // the old one the same way.
    bool unregister_transform(const std::string& name);

    // Remove every custom transform registered with `owner` (nullptr: none) and
    // revoke it in every retained copy, including steps of saved pipelines, so
    // no callback into the owner stays reachable.  Returns the number removed.
    size_t unregister_owner(const void* owner);

    // Enable/disable every custom transform registered with `owner`.  Disabled
    // transforms stay registered but are not listed by available_transforms()
    // and get_transform() does not return them; retained copies (pipelines,
    // saved pipelines) apply them as identity until re-enabled.
    void set_owner_enabled(const void* owner, bool enabled);

    // Number of custom transforms (enabled or not) registered with `owner`.
    size_t count_owned(const void* owner) const;

    // Get a registered (enabled) custom or built-in transform by name
    bool get_transform(const std::string& name, DataTransform& out) const;

    // Get all available transform names (built-in + enabled custom)
    std::vector<std::string> available_transforms() const;

    // Save a pipeline preset
    void save_pipeline(const std::string& name, const TransformPipeline& pipeline);

    // Load a pipeline preset.  Like get_transform(), this fails (and leaves
    // `out` untouched) while an enabled step is unavailable because its plugin
    // is disabled or unloaded.  A step whose plugin was unloaded is rebound by
    // name to a transform registered again since (e.g. the plugin reloaded).
    bool load_pipeline(const std::string& name, TransformPipeline& out) const;

    // Get all saved pipeline names
    std::vector<std::string> saved_pipelines() const;

    // Remove a saved pipeline
    bool remove_pipeline(const std::string& name);

    // Create a DataTransform from a TransformType (factory)
    static DataTransform create(TransformType type, const TransformParams& params = {});

   private:
    struct CustomEntry
    {
        DataTransform transform;
        std::string   description;
        const void*   owner   = nullptr;
        bool          enabled = true;
    };

    // Creates the shared state of a registered transform.  Set by the
    // constructor, so it always points into the module that owns the registry
    // (the host): a plugin linking spectra statically (Windows) runs its own
    // copy of register_transform(), and a shared_ptr control block allocated
    // there would be released by retained copies after the plugin unloaded.
    // The callbacks themselves are dropped by unregister_owner() before unload.
    using StateFactory       = std::shared_ptr<DataTransform::CustomState> (*)();
    StateFactory make_state_ = nullptr;

    static std::shared_ptr<DataTransform::CustomState> make_custom_state();

    mutable std::mutex                                 mutex_;
    std::unordered_map<std::string, CustomEntry>       custom_transforms_;
    std::unordered_map<std::string, TransformPipeline> saved_pipelines_;

    void register_builtins();
};

// ─── Convenience free functions ─────────────────────────────────────────────

// Apply a single transform type to Y data
[[nodiscard]] std::vector<float> transform_y(std::span<const float> y,
                                             TransformType          type,
                                             const TransformParams& params = {});

// Apply a single transform to X-Y data
void transform_xy(std::span<const float> x_in,
                  std::span<const float> y_in,
                  std::vector<float>&    x_out,
                  std::vector<float>&    y_out,
                  TransformType          type,
                  const TransformParams& params = {});

// Get the human-readable name for a transform type
const char* transform_type_name(TransformType type);

}   // namespace spectra
