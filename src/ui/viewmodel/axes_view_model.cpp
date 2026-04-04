#include "ui/viewmodel/axes_view_model.hpp"

#include <cmath>

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

AxesViewModel::AxesViewModel(Axes* model) : model_(model) {}

// ── Visual-limit accessors (Phase 3: local storage with model fallback) ──────

AxisLimits AxesViewModel::visual_xlim() const
{
    if (xlim_.has_value())
        return *xlim_;
    if (model_)
        return model_->x_limits();
    return {0.0, 1.0};
}

AxisLimits AxesViewModel::visual_ylim() const
{
    if (ylim_.has_value())
        return *ylim_;
    if (model_)
        return model_->y_limits();
    return {0.0, 1.0};
}

void AxesViewModel::set_visual_xlim(double min, double max)
{
    if (!model_)
        return;
    // Validate: reject degenerate, NaN, or infinite ranges
    if (std::isnan(min) || std::isnan(max) || std::isinf(min) || std::isinf(max))
        return;
    if (min >= max)
        return;
    AxisLimits old = visual_xlim();
    if (old.min == min && old.max == max)
        return;
    // Phase 3: store in ViewModel's local storage
    xlim_ = AxisLimits{min, max};
    // Sync to model for backward compatibility — callers that still read
    // directly from Axes will see the same limits.
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
    // Validate: reject degenerate, NaN, or infinite ranges
    if (std::isnan(min) || std::isnan(max) || std::isinf(min) || std::isinf(max))
        return;
    if (min >= max)
        return;
    AxisLimits old = visual_ylim();
    if (old.min == min && old.max == max)
        return;
    // Phase 3: store in ViewModel's local storage
    ylim_ = AxisLimits{min, max};
    // Sync to model for backward compatibility
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

void AxesViewModel::clear_visual_xlim()
{
    if (!xlim_.has_value())
        return;
    xlim_.reset();
    notify_changed(ChangeField::VisualXLim);
}

void AxesViewModel::clear_visual_ylim()
{
    if (!ylim_.has_value())
        return;
    ylim_.reset();
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
