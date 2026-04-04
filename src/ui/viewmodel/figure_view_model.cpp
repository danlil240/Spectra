#include "ui/viewmodel/figure_view_model.hpp"

#include <spectra/figure.hpp>

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

FigureViewModel::FigureViewModel(FigureId id, Figure* model)
    : figure_id_(id), model_(model)
{
}

void FigureViewModel::save_axes_state()
{
    if (!model_)
        return;

    axes_snapshots_.clear();
    for (const auto& ax : model_->axes())
    {
        if (ax)
        {
            AxesSnapshot snap;
            snap.x_limits = ax->x_limits();
            snap.y_limits = ax->y_limits();
            axes_snapshots_.push_back(snap);
        }
    }
    notify_changed(ChangeField::AxesSnapshots);
}

void FigureViewModel::restore_axes_state()
{
    if (!model_)
        return;

    for (size_t i = 0; i < axes_snapshots_.size() && i < model_->axes().size(); ++i)
    {
        if (model_->axes_mut()[i])
        {
            model_->axes_mut()[i]->xlim(axes_snapshots_[i].x_limits.min,
                                        axes_snapshots_[i].x_limits.max);
            model_->axes_mut()[i]->ylim(axes_snapshots_[i].y_limits.min,
                                        axes_snapshots_[i].y_limits.max);
        }
    }
}

// ── Setters with change notification + optional undo ─────────────────────

void FigureViewModel::set_selected_series_index(int idx)
{
    if (idx == selected_series_index_)
        return;
    int old = selected_series_index_;
    selected_series_index_ = idx;
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            "Change series selection",
            [self, old]() { self->suppressing_undo_ = true; self->set_selected_series_index(old); self->suppressing_undo_ = false; },
            [self, idx]() { self->suppressing_undo_ = true; self->set_selected_series_index(idx); self->suppressing_undo_ = false; }});
    }
    notify_changed(ChangeField::SelectedSeriesIndex);
}

void FigureViewModel::set_selected_axes_index(int idx)
{
    if (idx == selected_axes_index_)
        return;
    selected_axes_index_ = idx;
    notify_changed(ChangeField::SelectedAxesIndex);
}

void FigureViewModel::set_inspector_scroll_y(float y)
{
    if (y == inspector_scroll_y_)
        return;
    inspector_scroll_y_ = y;
    notify_changed(ChangeField::InspectorScrollY);
}

void FigureViewModel::set_is_modified(bool v)
{
    if (v == is_modified_)
        return;
    is_modified_ = v;
    notify_changed(ChangeField::IsModified);
}

void FigureViewModel::set_custom_title(const std::string& title)
{
    if (title == custom_title_)
        return;
    std::string old = custom_title_;
    custom_title_ = title;
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            "Rename figure",
            [self, old]() { self->suppressing_undo_ = true; self->set_custom_title(old); self->suppressing_undo_ = false; },
            [self, title]() { self->suppressing_undo_ = true; self->set_custom_title(title); self->suppressing_undo_ = false; }});
    }
    notify_changed(ChangeField::CustomTitle);
}

void FigureViewModel::set_is_in_3d_mode(bool v)
{
    if (v == is_in_3d_mode_)
        return;
    bool old = is_in_3d_mode_;
    is_in_3d_mode_ = v;
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            v ? "Switch to 3D" : "Switch to 2D",
            [self, old]() { self->suppressing_undo_ = true; self->set_is_in_3d_mode(old); self->suppressing_undo_ = false; },
            [self, v]() { self->suppressing_undo_ = true; self->set_is_in_3d_mode(v); self->suppressing_undo_ = false; }});
    }
    notify_changed(ChangeField::IsIn3DMode);
}

void FigureViewModel::set_saved_3d_camera(const Camera& cam)
{
    saved_3d_camera_ = cam;
    notify_changed(ChangeField::Saved3DCamera);
}

void FigureViewModel::set_home_limit(Axes* ax, const InitialLimits& lim)
{
    home_limits_[ax] = lim;
    notify_changed(ChangeField::HomeLimits);
}

void FigureViewModel::set_zoom_cache(float data_min, float data_max, size_t series_count)
{
    cached_data_min_          = data_min;
    cached_data_max_          = data_max;
    cached_zoom_series_count_ = series_count;
    zoom_cache_valid_         = true;
    notify_changed(ChangeField::ZoomCache);
}

void FigureViewModel::invalidate_zoom_cache()
{
    zoom_cache_valid_ = false;
}

void FigureViewModel::notify_changed(ChangeField field)
{
    if (on_changed_)
        on_changed_(*this, field);
}

// ── Sub-ViewModel management (LT-5) ─────────────────────────────────────────

AxesViewModel& FigureViewModel::get_or_create_axes_vm(Axes* ax)
{
    auto it = axes_vms_.find(ax);
    if (it != axes_vms_.end())
        return it->second;

    auto [inserted, _] = axes_vms_.emplace(ax, AxesViewModel(ax));
    inserted->second.set_undo_manager(undo_mgr_);
    return inserted->second;
}

SeriesViewModel& FigureViewModel::get_or_create_series_vm(Series* s)
{
    auto it = series_vms_.find(s);
    if (it != series_vms_.end())
        return it->second;

    auto [inserted, _] = series_vms_.emplace(s, SeriesViewModel(s));
    inserted->second.set_undo_manager(undo_mgr_);
    return inserted->second;
}

AxesViewModel* FigureViewModel::find_axes_vm(Axes* ax)
{
    auto it = axes_vms_.find(ax);
    return it != axes_vms_.end() ? &it->second : nullptr;
}

SeriesViewModel* FigureViewModel::find_series_vm(Series* s)
{
    auto it = series_vms_.find(s);
    return it != series_vms_.end() ? &it->second : nullptr;
}

void FigureViewModel::remove_axes_vm(Axes* ax)
{
    axes_vms_.erase(ax);
}

void FigureViewModel::remove_series_vm(Series* s)
{
    series_vms_.erase(s);
}

}   // namespace spectra
