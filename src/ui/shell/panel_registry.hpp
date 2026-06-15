#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <map>
    #include <memory>
    #include <string>
    #include <string_view>
    #include <unordered_map>
    #include <vector>
    #include "ui/shell/panel.hpp"

namespace spectra::ui::shell
{
class PanelRegistry
{
   public:
    PanelRegistry()  = default;
    ~PanelRegistry() = default;

    PanelRegistry(const PanelRegistry&)            = delete;
    PanelRegistry& operator=(const PanelRegistry&) = delete;

    Panel* add(std::unique_ptr<Panel> p);
    Panel* find(std::string_view id) const;
    bool   set_visible(std::string_view id, bool v);
    bool   toggle(std::string_view id);

    void draw_all();

    std::vector<Panel*>      all() const;
    std::vector<Panel*>      in_category(std::string_view cat) const;
    std::vector<std::string> categories() const;

    std::map<std::string, bool> capture_visibility() const;
    void                        apply_visibility(const std::map<std::string, bool>& v);

   private:
    std::vector<std::unique_ptr<Panel>>     panels_;
    std::unordered_map<std::string, Panel*> by_id_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
