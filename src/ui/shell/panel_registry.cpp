#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/panel_registry.hpp"

    #include <algorithm>

namespace spectra::ui::shell
{
Panel* PanelRegistry::add(std::unique_ptr<Panel> p)
{
    if (!p)
        return nullptr;

    std::string id = p->id();
    if (by_id_.contains(id))
    {
        // Duplicate ids are rejected (nullptr); callers must use unique ids.
        return nullptr;
    }

    // Push to the vector first so a throwing push_back cannot leave a dangling
    // raw pointer in by_id_.
    panels_.push_back(std::move(p));
    Panel* raw            = panels_.back().get();
    by_id_[std::move(id)] = raw;
    return raw;
}

Panel* PanelRegistry::find(std::string_view id) const
{
    const auto it = by_id_.find(std::string(id));
    if (it == by_id_.end())
        return nullptr;
    return it->second;
}

bool PanelRegistry::set_visible(std::string_view id, bool v)
{
    Panel* panel = find(id);
    if (!panel)
        return false;
    panel->set_visible(v);
    return true;
}

bool PanelRegistry::toggle(std::string_view id)
{
    Panel* panel = find(id);
    if (!panel)
        return false;
    panel->set_visible(!panel->visible());
    return true;
}

void PanelRegistry::draw_all()
{
    for (const auto& panel : panels_)
    {
        if (panel->visible())
            panel->draw();
    }
}

std::vector<Panel*> PanelRegistry::all() const
{
    std::vector<Panel*> result;
    result.reserve(panels_.size());
    for (const auto& panel : panels_)
        result.push_back(panel.get());
    return result;
}

std::vector<Panel*> PanelRegistry::in_category(std::string_view cat) const
{
    std::vector<Panel*> result;
    for (const auto& panel : panels_)
    {
        if (panel->category() == cat)
            result.push_back(panel.get());
    }
    return result;
}

std::vector<std::string> PanelRegistry::categories() const
{
    std::vector<std::string> result;
    for (const auto& panel : panels_)
    {
        const std::string& cat = panel->category();
        if (std::find(result.begin(), result.end(), cat) == result.end())
            result.push_back(cat);
    }
    return result;
}

std::map<std::string, bool> PanelRegistry::capture_visibility() const
{
    std::map<std::string, bool> result;
    for (const auto& panel : panels_)
        result.emplace(panel->id(), panel->visible());
    return result;
}

void PanelRegistry::apply_visibility(const std::map<std::string, bool>& v)
{
    for (const auto& [id, visible] : v)
    {
        if (Panel* panel = find(id))
            panel->set_visible(visible);
    }
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
