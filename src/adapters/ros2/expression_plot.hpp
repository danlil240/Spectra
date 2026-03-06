#pragma once

// ExpressionPlot — computed/derived series for ROS2 live plotting (C5).
//
// Binds a compiled ExpressionEngine to a set of GenericSubscriber ring
// buffers and a Spectra LineSeries.  Each frame the caller invokes poll()
// to drain the ring buffers, evaluate the expression for each new timestamp,
// and append the result to the series.
//
// Variable resolution:
//   Variable names in the expression (e.g. "$imu.acc.x") are mapped to
//   (topic, field_path) subscriptions at add_variable() time.  The engine
//   evaluates with the most recently received value for each variable; a
//   variable that has never been received evaluates to NaN, which propagates
//   to the output sample.
//
// Typical usage:
//   Ros2Bridge bridge;  bridge.init(…);  bridge.start_spin();
//   MessageIntrospector intr;
//   ExpressionPlot ep(bridge, intr);
//
//   ep.add_variable("$ax", "/imu", "linear_acceleration.x");
//   ep.add_variable("$ay", "/imu", "linear_acceleration.y");
//   ep.add_variable("$az", "/imu", "linear_acceleration.z");
//   ep.set_expression("sqrt($ax^2 + $ay^2 + $az^2)");
//
//   // In render loop:
//   ep.poll();           // drain ring buffers, evaluate, append to series
//   ep.axes();           // axes containing the LineSeries
//
// Timestamp policy:
//   When multiple variables are ticked in a single poll() call, the timestamp
//   of the first-received sample triggering evaluation is used for the output.
//   If a variable has no new data, the last known value is held (zero-order hold).
//
// Thread-safety:
//   All methods must be called from the render thread.
//   GenericSubscriber ring buffers are SPSC (executor = producer, render = consumer).

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "expression_engine.hpp"
#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// ExpressionPlot
// ---------------------------------------------------------------------------

class ExpressionPlot
{
public:
    // Maximum samples drained per variable per poll() call.
    static constexpr size_t MAX_DRAIN_PER_POLL = 4096;

    // Number of output samples after which the first Y auto-fit fires.
    static constexpr size_t AUTO_FIT_SAMPLES = 100;

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    ExpressionPlot(Ros2Bridge& bridge, MessageIntrospector& intr);
    ~ExpressionPlot();

    // Non-copyable, non-movable.
    ExpressionPlot(const ExpressionPlot&)            = delete;
    ExpressionPlot& operator=(const ExpressionPlot&) = delete;
    ExpressionPlot(ExpressionPlot&&)                 = delete;
    ExpressionPlot& operator=(ExpressionPlot&&)      = delete;

    // ------------------------------------------------------------------
    // Expression
    // ------------------------------------------------------------------

    // Compile an expression string.  Variables must have already been
    // registered via add_variable() before calling set_expression(), OR
    // the expression may reference only variables registered afterwards —
    // both orders work because variable lookup is deferred to evaluate().
    // Returns true if the expression compiles successfully.
    bool set_expression(const std::string& expr);

    // True when a valid expression has been compiled.
    bool is_compiled() const;

    // Last compile error (empty when is_compiled() == true).
    const std::string& compile_error() const { return compile_error_; }

    // The expression string.
    const std::string& expression() const;

    // ------------------------------------------------------------------
    // Variable binding
    // ------------------------------------------------------------------

    // Bind a variable name (e.g. "$ax") to a (topic, field_path) subscription.
    // type_name — ROS2 message type; empty = auto-detect.
    // buffer_depth — ring buffer capacity.
    // Returns true on success; false if the topic/field could not be
    // subscribed (e.g. bridge not spinning, field not found in schema).
    bool add_variable(const std::string& var_name,
                      const std::string& topic,
                      const std::string& field_path,
                      const std::string& type_name   = "",
                      size_t             buffer_depth = 10000);

    // Remove a variable binding and unsubscribe.  Returns false if not found.
    bool remove_variable(const std::string& var_name);

    // Remove all variable bindings.
    void clear_variables();

    // Names of currently registered variables.
    std::vector<std::string> variable_names() const;

    // True when a variable is registered.
    bool has_variable(const std::string& var_name) const;

    // ------------------------------------------------------------------
    // Frame update (render thread)
    // ------------------------------------------------------------------

    // Drain ring buffers, evaluate expression, append output samples to series.
    // Call once per frame from the render thread.
    void poll();

    // ------------------------------------------------------------------
    // Figure / Series access
    // ------------------------------------------------------------------

    spectra::Figure& figure() { return *figure_; }
    const spectra::Figure& figure() const { return *figure_; }

    spectra::Axes& axes() { return *axes_; }
    const spectra::Axes& axes() const { return *axes_; }

    spectra::LineSeries* series() { return series_; }
    const spectra::LineSeries* series() const { return series_; }

    // ------------------------------------------------------------------
    // Auto-scroll (C2)
    // ------------------------------------------------------------------

    void set_time_window(double seconds);
    double time_window() const;

    void pause_scroll();
    void resume_scroll();
    bool is_scroll_paused() const;

    size_t memory_bytes() const;

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    void set_figure_size(uint32_t w, uint32_t h);
    void set_label(const std::string& label);
    const std::string& label() const { return label_; }

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    // Called from poll() for each output sample appended.
    using OnDataCallback = std::function<void(double t_sec, double value)>;
    void set_on_data(OnDataCallback cb) { on_data_cb_ = std::move(cb); }

    // ------------------------------------------------------------------
    // Presets (delegated to ExpressionEngine)
    // ------------------------------------------------------------------

    void save_preset(const std::string& name) { engine_.save_preset(name); }
    bool load_preset(const std::string& name);
    bool remove_preset(const std::string& name) { return engine_.remove_preset(name); }
    std::vector<ExpressionPreset> presets() const { return engine_.presets(); }
    std::string serialize_presets() const { return engine_.serialize_presets(); }
    void deserialize_presets(const std::string& json) { engine_.deserialize_presets(json); }

    // ------------------------------------------------------------------
    // Engine access (for testing / editor integration)
    // ------------------------------------------------------------------

    ExpressionEngine& engine() { return engine_; }
    const ExpressionEngine& engine() const { return engine_; }

private:
    // ------------------------------------------------------------------
    // VarEntry — one bound variable
    // ------------------------------------------------------------------
    struct VarEntry
    {
        std::string var_name;   // e.g. "$ax"
        std::string topic;
        std::string field_path;
        std::string type_name;

        std::unique_ptr<GenericSubscriber> subscriber;
        int extractor_id{-1};

        // Latest received value (zero-order hold).
        double last_value{0.0};
        bool   received_any{false};

        // Per-variable drain scratch (grows-only).
        std::vector<FieldSample> drain_buf;
    };

    // Auto-detect message type from graph.
    std::string detect_type(const std::string& topic) const;

    // Rebuild the figure label from expression.
    void rebuild_series_label();

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    Ros2Bridge&          bridge_;
    MessageIntrospector& intr_;

    ExpressionEngine engine_;

    std::vector<VarEntry> vars_;

    // Output figure + series.
    std::unique_ptr<spectra::Figure> figure_;
    spectra::Axes*       axes_{nullptr};
    spectra::LineSeries* series_{nullptr};

    // Auto-fit state.
    size_t output_count_{0};
    bool   auto_fitted_{false};

    // Time origin for relative timestamps.
    double time_origin_{0.0};
    bool   has_time_origin_{false};

    // User-supplied label; empty = use expression string.
    std::string label_;

    // Last compile error.
    std::string compile_error_;

    OnDataCallback on_data_cb_;
};

}   // namespace spectra::adapters::ros2
