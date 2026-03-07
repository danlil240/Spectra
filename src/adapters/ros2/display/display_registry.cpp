#include "display/display_registry.hpp"

#include <algorithm>

namespace spectra::adapters::ros2
{

bool DisplayRegistry::register_factory(
    const DisplayTypeInfo& info,
    std::function<std::unique_ptr<DisplayPlugin>()> factory)
{
    if (!factory || info.type_id.empty() || factories_.find(info.type_id) != factories_.end())
        return false;

    factories_[info.type_id] = std::move(factory);
    ordered_types_.push_back(info);
    std::sort(ordered_types_.begin(),
              ordered_types_.end(),
              [](const DisplayTypeInfo& a, const DisplayTypeInfo& b)
              {
                  if (a.display_name == b.display_name)
                      return a.type_id < b.type_id;
                  return a.display_name < b.display_name;
              });
    return true;
}

std::unique_ptr<DisplayPlugin> DisplayRegistry::create(const std::string& type_id) const
{
    const auto it = factories_.find(type_id);
    return it != factories_.end() ? it->second() : nullptr;
}

std::vector<DisplayTypeInfo> DisplayRegistry::list_types() const
{
    return ordered_types_;
}

}   // namespace spectra::adapters::ros2
