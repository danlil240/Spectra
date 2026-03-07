#pragma once

#include <memory>
#include <string>
#include <vector>

#include "display/display_registry.hpp"

namespace spectra::adapters::ros2
{

class DisplaysPanel
{
public:
    void set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    int selected_index() const { return selected_index_; }
    void set_selected_index(int index) { selected_index_ = index; }

    void draw(bool* p_open,
              const DisplayRegistry& registry,
              const DisplayContext& context,
              std::vector<std::unique_ptr<DisplayPlugin>>& displays);

private:
    std::string title_{"Displays"};
    int selected_index_{-1};
};

}   // namespace spectra::adapters::ros2
