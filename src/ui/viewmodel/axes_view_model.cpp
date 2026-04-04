#include "ui/viewmodel/axes_view_model.hpp"

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

AxesViewModel::AxesViewModel(Axes* model) : model_(model) {}

// ── Forwarding accessors (Phase 1: delegate to Axes model) ───────────────

AxisLimits AxesViewModel::visual_xlim() const
{
    if (model_)
        return model_->x_limits();
    return {0.0, 1.0};
}

AxisLimits AxesViewModel::visual_ylim() const
{
    if (model_)
        return model_->y_limits();
    return {0.0, 1.0};
}

void AxesViewModel::set_visual_xlim(double min, double max)
{
    if (!model_)
        return;
    AxisLimits old = model_->x_limits();
    if (old.min == min && old.max == max)
        return;
    model_->xlim(min, max);
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{"Change X limits",
                                   [self, old]()
                                   {
                                       self->suppressing_undo_ = true;
                                       self->set_visual_xlim(old.min, old.max);
                                       self->suppressing_undo_ = false;
                                   },
                                   [self, min, max]()
                                   {
                                       self->suppressing_undo_ = true;
                                       self->set_visual_xlim(min, max);
                                       self->suppressing_undo_ = false;
                                   }});
    }
    notify_changed(ChangeField::VisualXLim);
}

void AxesViewModel::set_visual_ylim(double min, double max)
{
    if (!model_)
        return;
    AxisLimits old = model_->y_limits();
    if (old.min == min && old.max == max)
        return;
    model_->ylim(min, max);
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{"Change Y limits",
                                   [self, old]()
                                   {
                                       self->suppressing_undo_ = true;
                                       self->set_visual_ylim(old.min, old.max);
                                       self->suppressing_undo_ = false;
                                   },
                                   [self, min, max]()
                                   {
                                       self->suppressing_undo_ = true;
                                       self->set_visual_ylim(min, max);
                                       self->suppressing_undo_ = false;
                                   }});
    }
    notify_changed(ChangeField::VisualYLim);
}

// ── New per-view state setters ───────────────────────────────────────────

void AxesViewModel::set_is_hovered(bool v)
{
    if (v == is_hovered_)
        return;
    is_hovered_ = v;
    notify_changed(ChangeField::IsHovered);
}

void AxesViewModel::set_scroll_y(float y)
{
    if (y == scroll_y_)
        return;
    scroll_y_ = y;
    notify_changed(ChangeField::ScrollY);
}

void AxesViewModel::notify_changed(ChangeField field)
{
    if (on_changed_)
        on_changed_(*this, field);
}

}   // namespace spectra
