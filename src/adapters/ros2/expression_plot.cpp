// ExpressionPlot — implementation.
//
// See expression_plot.hpp for design notes.

#include "expression_plot.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <spectra/color.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ExpressionPlot::ExpressionPlot(Ros2Bridge& bridge, MessageIntrospector& intr)
    : bridge_(bridge)
    , intr_(intr)
{
    spectra::FigureConfig cfg;
    cfg.width  = 1280;
    cfg.height = 720;
    figure_    = std::make_unique<spectra::Figure>(cfg);

    // Create a single 1×1 subplot axes.
    axes_   = &figure_->subplot(1, 1, 1);
    spectra::LineSeries& ls = axes_->line();
    ls.label("expression");
    ls.color(spectra::palette::default_cycle[0]);
    series_ = &ls;

    axes_->xlabel("time (s)");
    axes_->ylabel("value");
}

ExpressionPlot::~ExpressionPlot()
{
    clear_variables();
}

// ---------------------------------------------------------------------------
// Expression
// ---------------------------------------------------------------------------

bool ExpressionPlot::set_expression(const std::string& expr)
{
    compile_error_.clear();
    auto result = engine_.compile(expr);
    if (!result.ok)
    {
        compile_error_ = result.error;
        return false;
    }
    rebuild_series_label();
    return true;
}

bool ExpressionPlot::is_compiled() const
{
    return engine_.is_compiled();
}

const std::string& ExpressionPlot::expression() const
{
    return engine_.expression();
}

// ---------------------------------------------------------------------------
// Variable binding
// ---------------------------------------------------------------------------

bool ExpressionPlot::add_variable(const std::string& var_name,
                                  const std::string& topic,
                                  const std::string& field_path,
                                  const std::string& type_name,
                                  size_t             buffer_depth)
{
    if (var_name.empty() || topic.empty() || field_path.empty())
        return false;

    // Resolve type.
    std::string resolved_type = type_name;
    if (resolved_type.empty())
    {
        resolved_type = detect_type(topic);
        if (resolved_type.empty())
            return false;
    }

    // Remove any existing binding for this var_name.
    remove_variable(var_name);

    VarEntry ve;
    ve.var_name   = var_name;
    ve.topic      = topic;
    ve.field_path = field_path;
    ve.type_name  = resolved_type;
    ve.last_value = 0.0;
    ve.received_any = false;

    if (bridge_.is_ok())
    {
        auto sub = std::make_unique<GenericSubscriber>(
            bridge_.node(), topic, resolved_type, intr_, buffer_depth);

        int eid = sub->add_field(field_path);
        if (eid < 0)
            return false;

        ve.extractor_id = eid;

        if (!sub->start())
            return false;

        ve.drain_buf.reserve(std::min(buffer_depth, MAX_DRAIN_PER_POLL));
        ve.drain_buf.clear();
        ve.subscriber = std::move(sub);
    }

    // Initialise engine variable to 0.
    engine_.set_variable(var_name, 0.0);

    vars_.push_back(std::move(ve));
    return true;
}

bool ExpressionPlot::remove_variable(const std::string& var_name)
{
    auto it = std::find_if(vars_.begin(), vars_.end(),
                           [&var_name](const VarEntry& v){ return v.var_name == var_name; });
    if (it == vars_.end())
        return false;

    if (it->subscriber)
        it->subscriber->stop();

    vars_.erase(it);
    return true;
}

void ExpressionPlot::clear_variables()
{
    for (auto& ve : vars_)
    {
        if (ve.subscriber)
            ve.subscriber->stop();
    }
    vars_.clear();
}

std::vector<std::string> ExpressionPlot::variable_names() const
{
    std::vector<std::string> names;
    names.reserve(vars_.size());
    for (const auto& ve : vars_)
        names.push_back(ve.var_name);
    return names;
}

bool ExpressionPlot::has_variable(const std::string& var_name) const
{
    return std::any_of(vars_.begin(), vars_.end(),
                       [&var_name](const VarEntry& v){ return v.var_name == var_name; });
}

// ---------------------------------------------------------------------------
// poll() — hot path
// ---------------------------------------------------------------------------

void ExpressionPlot::poll()
{
    const double wall_now =
        std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    // Set time origin on first frame for float precision.
    if (!scroll_.has_time_origin())
        scroll_.set_time_origin(wall_now);

    scroll_.set_now(wall_now);

    if (!engine_.is_compiled())
    {
        scroll_.tick(series_, axes_);
        return;
    }

    // For each variable, drain its ring buffer and update last_value.
    // We track the newest timestamp seen across all variables.
    double newest_t_sec = 0.0;
    size_t total_new    = 0;

    for (auto& ve : vars_)
    {
        if (!ve.subscriber || !ve.subscriber->is_running() || ve.extractor_id < 0)
            continue;

        if (ve.drain_buf.capacity() < MAX_DRAIN_PER_POLL)
            ve.drain_buf.reserve(MAX_DRAIN_PER_POLL);

        const size_t n =
            ve.subscriber->pop_bulk(ve.extractor_id, ve.drain_buf.data(), MAX_DRAIN_PER_POLL);

        if (n > 0)
        {
            // Use the last (most recent) sample for zero-order hold.
            const FieldSample& last = ve.drain_buf[n - 1];
            ve.last_value   = last.value;
            ve.received_any = true;

            const double t = static_cast<double>(last.timestamp_ns) * 1e-9;
            if (t > newest_t_sec)
                newest_t_sec = t;

            engine_.set_variable(ve.var_name, ve.last_value);
            total_new += n;
        }
        else
        {
            // No new data — keep last_value in the engine (already set).
            engine_.set_variable(ve.var_name, ve.last_value);
        }
    }

    // Only emit an output sample if at least one variable received new data.
    if (total_new > 0 && newest_t_sec > 0.0)
    {
        const double result = engine_.evaluate();
        if (!std::isnan(result))
        {
            // Use relative time (seconds since scroll origin) to preserve
            // float precision at epoch-scale timestamps.
            const double origin = scroll_.time_origin();
            const double t_rel  = newest_t_sec - origin;
            series_->append(static_cast<float>(t_rel),
                            static_cast<float>(result));

            if (on_data_cb_)
                on_data_cb_(newest_t_sec, result);

            ++output_count_;

            if (!auto_fitted_ && output_count_ >= AUTO_FIT_SAMPLES)
            {
                axes_->auto_fit();
                auto_fitted_ = true;
            }
        }
    }

    scroll_.tick(series_, axes_);
}

// ---------------------------------------------------------------------------
// Auto-scroll
// ---------------------------------------------------------------------------

void ExpressionPlot::set_time_window(double seconds)
{
    scroll_.set_window_s(seconds);
}

double ExpressionPlot::time_window() const
{
    return scroll_.window_s();
}

void ExpressionPlot::pause_scroll()  { scroll_.pause(); }
void ExpressionPlot::resume_scroll() { scroll_.resume(); }
bool ExpressionPlot::is_scroll_paused() const { return scroll_.is_paused(); }

size_t ExpressionPlot::memory_bytes() const
{
    return ScrollController::memory_bytes(series_);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ExpressionPlot::set_figure_size(uint32_t w, uint32_t h)
{
    figure_->set_size(w, h);
}

void ExpressionPlot::set_label(const std::string& label)
{
    label_ = label;
    if (series_)
        series_->label(label_.empty() ? engine_.expression() : label_);
}

// ---------------------------------------------------------------------------
// Preset loading
// ---------------------------------------------------------------------------

bool ExpressionPlot::load_preset(const std::string& name)
{
    if (!engine_.load_preset(name))
        return false;
    compile_error_.clear();
    rebuild_series_label();
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string ExpressionPlot::detect_type(const std::string& topic) const
{
    // Do NOT call node_->get_topic_names_and_types() here — that goes
    // through the DDS graph layer and can deadlock with rmw_fastrtps's
    // discovery thread when namespaced participants are present.
    // The caller should supply the type_name via add_variable() instead.
    (void)topic;
    return {};
}

void ExpressionPlot::rebuild_series_label()
{
    if (!series_)
        return;
    const std::string& lbl = label_.empty() ? engine_.expression() : label_;
    series_->label(lbl);
    axes_->ylabel(lbl);
}

}   // namespace spectra::adapters::ros2
