#include "knob_manager.hpp"

#include <algorithm>

namespace spectra
{

Knob& KnobManager::add_float(const std::string& name,
                              float default_val,
                              float min_val,
                              float max_val,
                              float step,
                              std::function<void(float)> on_change)
{
    std::lock_guard<std::mutex> lock(mutex_);
    knobs_.push_back({name, KnobType::Float, default_val, min_val, max_val, step, {}, std::move(on_change)});
    return knobs_.back();
}

Knob& KnobManager::add_int(const std::string& name,
                            int default_val,
                            int min_val,
                            int max_val,
                            std::function<void(float)> on_change)
{
    std::lock_guard<std::mutex> lock(mutex_);
    knobs_.push_back({name, KnobType::Int, static_cast<float>(default_val),
                      static_cast<float>(min_val), static_cast<float>(max_val),
                      1.0f, {}, std::move(on_change)});
    return knobs_.back();
}

Knob& KnobManager::add_bool(const std::string& name,
                             bool default_val,
                             std::function<void(float)> on_change)
{
    std::lock_guard<std::mutex> lock(mutex_);
    knobs_.push_back({name, KnobType::Bool, default_val ? 1.0f : 0.0f,
                      0.0f, 1.0f, 1.0f, {}, std::move(on_change)});
    return knobs_.back();
}

Knob& KnobManager::add_choice(const std::string& name,
                               const std::vector<std::string>& choices,
                               int default_index,
                               std::function<void(float)> on_change)
{
    std::lock_guard<std::mutex> lock(mutex_);
    float max_idx = choices.empty() ? 0.0f : static_cast<float>(choices.size() - 1);
    knobs_.push_back({name, KnobType::Choice, static_cast<float>(default_index),
                      0.0f, max_idx, 1.0f, choices, std::move(on_change)});
    return knobs_.back();
}

Knob* KnobManager::find(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& k : knobs_)
    {
        if (k.name == name)
            return &k;
    }
    return nullptr;
}

const Knob* KnobManager::find(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& k : knobs_)
    {
        if (k.name == name)
            return &k;
    }
    return nullptr;
}

float KnobManager::value(const std::string& name, float default_val) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& k : knobs_)
    {
        if (k.name == name)
            return k.value;
    }
    return default_val;
}

bool KnobManager::set_value(const std::string& name, float new_value)
{
    std::function<void(float)> per_knob_cb;
    std::function<void()> any_cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& k : knobs_)
        {
            if (k.name == name)
            {
                float clamped = std::clamp(new_value, k.min_val, k.max_val);
                if (clamped == k.value)
                    return true;
                k.value = clamped;
                per_knob_cb = k.on_change;
                any_cb = on_any_change_;
                break;
            }
        }
        if (!per_knob_cb && !any_cb)
            return false;
    }
    // Fire callbacks outside lock
    if (per_knob_cb)
        per_knob_cb(new_value);
    if (any_cb)
        any_cb();
    return true;
}

size_t KnobManager::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return knobs_.size();
}

bool KnobManager::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return knobs_.empty();
}

std::deque<Knob>& KnobManager::knobs()
{
    return knobs_;
}

const std::deque<Knob>& KnobManager::knobs() const
{
    return knobs_;
}

void KnobManager::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    knobs_.clear();
}

bool KnobManager::remove(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(knobs_.begin(), knobs_.end(),
                             [&](const Knob& k) { return k.name == name; });
    if (it == knobs_.end())
        return false;
    knobs_.erase(it, knobs_.end());
    return true;
}

void KnobManager::set_on_any_change(std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    on_any_change_ = std::move(cb);
}

void KnobManager::notify_any_changed()
{
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = on_any_change_;
    }
    if (cb)
        cb();
}

void KnobManager::mark_dirty(const std::string& name, float value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_changes_.emplace_back(name, value);
}

std::vector<std::pair<std::string, float>> KnobManager::take_pending_changes()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, float>> out;
    out.swap(pending_changes_);
    return out;
}

}  // namespace spectra
