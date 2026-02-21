#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace spectra
{

// Type of interactive knob control.
enum class KnobType
{
    Float,   // Continuous float slider
    Int,     // Integer slider (value stored as float, displayed as int)
    Bool,    // Checkbox toggle (0.0 = false, 1.0 = true)
    Choice,  // Dropdown from a list of named options (value = selected index)
};

// A single interactive knob parameter.
struct Knob
{
    std::string name;
    KnobType type = KnobType::Float;
    float value = 0.0f;
    float min_val = 0.0f;
    float max_val = 1.0f;
    float step = 0.0f;                 // 0 = continuous (Float), 1 (Int/Choice)
    std::vector<std::string> choices;  // For KnobType::Choice
    std::function<void(float)> on_change;

    // Convenience accessors
    int int_value() const { return static_cast<int>(value); }
    bool bool_value() const { return value >= 0.5f; }
    int choice_index() const { return static_cast<int>(value); }
};

// Manages a collection of interactive knob parameters that appear as
// an overlay panel on the plot canvas.  Users define knobs before
// calling show(); the ImGui integration draws them every frame and
// fires callbacks when values change.
//
// Thread-safe: all public methods lock an internal mutex.
class KnobManager
{
   public:
    KnobManager() = default;
    ~KnobManager() = default;

    KnobManager(const KnobManager&) = delete;
    KnobManager& operator=(const KnobManager&) = delete;

    // ── Add knobs ────────────────────────────────────────────────────

    // Add a float slider knob.  Returns reference to the created Knob.
    Knob& add_float(const std::string& name,
                    float default_val,
                    float min_val,
                    float max_val,
                    float step = 0.0f,
                    std::function<void(float)> on_change = nullptr);

    // Add an integer slider knob.
    Knob& add_int(const std::string& name,
                  int default_val,
                  int min_val,
                  int max_val,
                  std::function<void(float)> on_change = nullptr);

    // Add a boolean checkbox knob.
    Knob& add_bool(const std::string& name,
                   bool default_val,
                   std::function<void(float)> on_change = nullptr);

    // Add a choice dropdown knob.
    Knob& add_choice(const std::string& name,
                     const std::vector<std::string>& choices,
                     int default_index = 0,
                     std::function<void(float)> on_change = nullptr);

    // ── Query ────────────────────────────────────────────────────────

    // Find a knob by name.  Returns nullptr if not found.
    Knob* find(const std::string& name);
    const Knob* find(const std::string& name) const;

    // Get value of a knob by name.  Returns default_val if not found.
    float value(const std::string& name, float default_val = 0.0f) const;

    // Set value of a knob by name (triggers callback).  Returns false if not found.
    bool set_value(const std::string& name, float new_value);

    // Number of registered knobs.
    size_t count() const;

    // Whether any knobs are registered.
    bool empty() const;

    // Access all knobs (for ImGui rendering).
    std::deque<Knob>& knobs();
    const std::deque<Knob>& knobs() const;

    // ── Lifecycle ────────────────────────────────────────────────────

    // Remove all knobs.
    void clear();

    // Remove a knob by name.
    bool remove(const std::string& name);

    // ── Panel state ──────────────────────────────────────────────────

    // Panel visibility (drawn by ImGuiIntegration).
    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }

    // Panel collapsed state.
    bool is_collapsed() const { return collapsed_; }
    void set_collapsed(bool c) { collapsed_ = c; }

    // Global on_change callback (fired after any knob changes, in addition
    // to the per-knob callback).  Useful for batch data recomputation.
    void set_on_any_change(std::function<void()> cb);

    // Fire the global on_any_change callback (called by ImGui draw code).
    void notify_any_changed();

    // ── Dirty tracking (for IPC) ─────────────────────────────────────

    // Record that a knob value changed (called by ImGui draw code).
    void mark_dirty(const std::string& name, float value);

    // Retrieve and clear pending changes (name → value).
    std::vector<std::pair<std::string, float>> take_pending_changes();

   private:
    mutable std::mutex mutex_;
    std::deque<Knob> knobs_;
    bool visible_ = true;
    bool collapsed_ = false;
    std::function<void()> on_any_change_;
    std::vector<std::pair<std::string, float>> pending_changes_;
};

}  // namespace spectra
