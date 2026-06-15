#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/nav_rail.hpp"

    #include <algorithm>
    #include <cctype>

    #include "imgui.h"
    #include "ui/imgui/widgets.hpp"
    #include "ui/shell/panel_registry.hpp"

namespace spectra::ui::shell
{
namespace
{
std::string to_lower_ascii(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}
}   // namespace

NavRail::NavRail(PanelRegistry* registry) : registry_(registry) {}

void NavRail::set_registry(PanelRegistry* r)
{
    registry_ = r;
}

PanelRegistry* NavRail::registry() const
{
    return registry_;
}

void NavRail::set_expanded(bool e)
{
    expanded_ = e;
}

bool NavRail::expanded() const
{
    return expanded_;
}

void NavRail::set_search_enabled(bool e)
{
    search_enabled_ = e;
}

bool NavRail::search_enabled() const
{
    return search_enabled_;
}

void NavRail::set_search(std::string text)
{
    search_ = std::move(text);
}

const std::string& NavRail::search() const
{
    return search_;
}

bool NavRail::matches_filter(std::string_view title, std::string_view filter)
{
    if (filter.empty())
        return true;

    const std::string title_lower  = to_lower_ascii(title);
    const std::string filter_lower = to_lower_ascii(filter);
    return title_lower.find(filter_lower) != std::string::npos;
}

void NavRail::build_items(std::vector<NavItem>& out) const
{
    PanelRegistry* reg = registry_;
    if (!reg)
        return;

    for (const std::string& category : reg->categories())
    {
        const std::vector<Panel*> panels = reg->in_category(category);

        bool category_has_match = search_.empty();
        if (!category_has_match)
        {
            for (const Panel* panel : panels)
            {
                if (matches_filter(panel->title(), search_))
                {
                    category_has_match = true;
                    break;
                }
            }
        }

        if (!category_has_match)
            continue;

        NavItem header;
        header.label             = category;
        header.section           = category;
        header.is_section_header = true;
        out.push_back(std::move(header));

        for (Panel* panel : panels)
        {
            if (!matches_filter(panel->title(), search_))
                continue;

            const std::string id = panel->id();

            NavItem item;
            item.icon      = panel->icon();
            item.label     = panel->title();
            item.tooltip   = panel->title();
            item.section   = category;
            item.is_active = [reg, id]()
            {
                Panel* p = reg->find(id);
                return p && p->visible();
            };
            item.on_click = [reg, id]() { reg->toggle(id); };
            out.push_back(std::move(item));
        }
    }
}

void NavRail::draw()
{
    if (search_enabled_)
    {
        char buf[256] = {};
        if (!search_.empty())
            std::copy_n(search_.begin(), std::min(search_.size(), sizeof(buf) - 1), buf);
        if (ImGui::InputText("##nav_rail_search", buf, sizeof(buf)))
            search_ = buf;
    }

    std::vector<NavItem> items;
    build_items(items);

    static bool section_open = true;

    for (const NavItem& item : items)
    {
        if (item.is_section_header)
        {
            if (expanded_)
                widgets::section_header(item.label.c_str(), &section_open);
            continue;
        }

        const bool active = item.is_active && item.is_active();
        const bool clicked =
            widgets::icon_button(item.label.c_str(), item.icon, item.tooltip.c_str(), active);

        if (expanded_)
            ImGui::SameLine();

        if (expanded_)
            ImGui::TextUnformatted(item.label.c_str());

        if (clicked && item.on_click)
            item.on_click();
    }
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
