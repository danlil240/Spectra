#include <spectra/figure_registry.hpp>

#include <algorithm>
#include <spectra/event_bus.hpp>

namespace spectra
{

FigureRegistry::IdType FigureRegistry::register_figure(std::unique_ptr<Figure> fig)
{
    IdType  id     = 0;
    Figure* figptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id     = next_id_++;
        figptr = fig.get();
        figures_[id] = std::move(fig);
        insertion_order_.push_back(id);
    }
    if (event_system_)
        event_system_->figure_created().emit({id, figptr});
    return id;
}

void FigureRegistry::unregister_figure(IdType id)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = figures_.find(id);
        if (it == figures_.end())
            return;
        figures_.erase(it);
        insertion_order_.erase(std::remove(insertion_order_.begin(), insertion_order_.end(), id),
                               insertion_order_.end());
    }
    if (event_system_)
        event_system_->figure_destroyed().emit({id});
}

Figure* FigureRegistry::get(IdType id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto                        it = figures_.find(id);
    return (it != figures_.end()) ? it->second.get() : nullptr;
}

std::vector<FigureRegistry::IdType> FigureRegistry::all_ids() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return insertion_order_;
}

size_t FigureRegistry::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return figures_.size();
}

bool FigureRegistry::contains(IdType id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return figures_.count(id) > 0;
}

FigureRegistry::IdType FigureRegistry::find_id(const Figure* fig) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, ptr] : figures_)
    {
        if (ptr.get() == fig)
            return id;
    }
    return 0;
}

std::unique_ptr<Figure> FigureRegistry::release(IdType id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto                        it = figures_.find(id);
    if (it == figures_.end())
        return nullptr;
    auto fig = std::move(it->second);
    figures_.erase(it);
    insertion_order_.erase(std::remove(insertion_order_.begin(), insertion_order_.end(), id),
                           insertion_order_.end());
    return fig;
}

void FigureRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    figures_.clear();
    insertion_order_.clear();
}

}   // namespace spectra
