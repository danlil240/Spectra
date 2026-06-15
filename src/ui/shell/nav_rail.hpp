#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <string>
    #include <vector>
    #include "ui/theme/icons.hpp"

namespace spectra::ui::shell
{
class PanelRegistry;

struct NavItem
{
    Icon                  icon = Icon::WindowIcon;
    std::string           label;
    std::string           tooltip;
    std::string           section;
    bool                  is_section_header = false;
    std::function<bool()> is_active;
    std::function<void()> on_click;
};

class NavRail
{
   public:
    explicit NavRail(PanelRegistry* registry = nullptr);
    virtual ~NavRail() = default;

    NavRail(const NavRail&)            = delete;
    NavRail& operator=(const NavRail&) = delete;

    void           set_registry(PanelRegistry* r);
    PanelRegistry* registry() const;

    void set_expanded(bool e);
    bool expanded() const;

    void set_search_enabled(bool e);
    bool search_enabled() const;

    void               set_search(std::string text);
    const std::string& search() const;

    virtual void build_items(std::vector<NavItem>& out) const;

    void draw();

   protected:
    static bool matches_filter(std::string_view title, std::string_view filter);

    PanelRegistry* registry_       = nullptr;
    bool           expanded_       = false;
    bool           search_enabled_ = false;
    std::string    search_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
