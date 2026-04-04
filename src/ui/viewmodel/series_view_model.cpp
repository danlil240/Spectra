#include "ui/viewmodel/series_view_model.hpp"

#include <spectra/series.hpp>

#include "ui/commands/undo_manager.hpp"

namespace spectra
{

SeriesViewModel::SeriesViewModel(Series* model)
    : model_(model)
{
}

// ── Effective accessors (override → model fallback) ──────────────────────

bool SeriesViewModel::effective_visible() const
{
    if (visible_override_.has_value())
        return *visible_override_;
    if (model_)
        return model_->visible();
    return true;
}

Color SeriesViewModel::effective_color() const
{
    if (color_override_.has_value())
        return *color_override_;
    if (model_)
        return model_->color();
    return colors::blue;
}

const std::string& SeriesViewModel::effective_label() const
{
    if (label_override_.has_value())
        return *label_override_;
    if (model_)
        return model_->label();
    static const std::string empty;
    return empty;
}

float SeriesViewModel::effective_opacity() const
{
    if (opacity_override_.has_value())
        return *opacity_override_;
    if (model_)
        return model_->opacity();
    return 1.0f;
}

// ── Override management ──────────────────────────────────────────────────

void SeriesViewModel::set_visible_override(bool v)
{
    if (visible_override_.has_value() && *visible_override_ == v)
        return;
    std::optional<bool> old = visible_override_;
    visible_override_       = v;
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            v ? "Show series" : "Hide series",
            [self, old]()
            {
                self->suppressing_undo_ = true;
                if (old.has_value())
                    self->set_visible_override(*old);
                else
                    self->clear_visible_override();
                self->suppressing_undo_ = false;
            },
            [self, v]()
            {
                self->suppressing_undo_ = true;
                self->set_visible_override(v);
                self->suppressing_undo_ = false;
            }});
    }
    notify_changed(ChangeField::Visible);
}

void SeriesViewModel::clear_visible_override()
{
    if (!visible_override_.has_value())
        return;
    visible_override_.reset();
    notify_changed(ChangeField::Visible);
}

void SeriesViewModel::set_color_override(const Color& c)
{
    if (color_override_.has_value() && color_override_->r == c.r && color_override_->g == c.g &&
        color_override_->b == c.b && color_override_->a == c.a)
        return;
    color_override_ = c;
    notify_changed(ChangeField::Color);
}

void SeriesViewModel::clear_color_override()
{
    if (!color_override_.has_value())
        return;
    color_override_.reset();
    notify_changed(ChangeField::Color);
}

void SeriesViewModel::set_label_override(const std::string& lbl)
{
    if (label_override_.has_value() && *label_override_ == lbl)
        return;
    std::optional<std::string> old = label_override_;
    label_override_                = lbl;
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            "Rename series",
            [self, old]()
            {
                self->suppressing_undo_ = true;
                if (old.has_value())
                    self->set_label_override(*old);
                else
                    self->clear_label_override();
                self->suppressing_undo_ = false;
            },
            [self, lbl]()
            {
                self->suppressing_undo_ = true;
                self->set_label_override(lbl);
                self->suppressing_undo_ = false;
            }});
    }
    notify_changed(ChangeField::Label);
}

void SeriesViewModel::clear_label_override()
{
    if (!label_override_.has_value())
        return;
    label_override_.reset();
    notify_changed(ChangeField::Label);
}

void SeriesViewModel::set_opacity_override(float o)
{
    if (opacity_override_.has_value() && *opacity_override_ == o)
        return;
    opacity_override_ = o;
    notify_changed(ChangeField::Opacity);
}

void SeriesViewModel::clear_opacity_override()
{
    if (!opacity_override_.has_value())
        return;
    opacity_override_.reset();
    notify_changed(ChangeField::Opacity);
}

// ── Direct forwarding to model (Phase 1) ─────────────────────────────────

void SeriesViewModel::set_visible(bool v)
{
    if (!model_)
        return;
    if (model_->visible() == v)
        return;
    bool old = model_->visible();
    model_->visible(v);
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            v ? "Show series" : "Hide series",
            [self, old]()
            {
                self->suppressing_undo_ = true;
                self->set_visible(old);
                self->suppressing_undo_ = false;
            },
            [self, v]()
            {
                self->suppressing_undo_ = true;
                self->set_visible(v);
                self->suppressing_undo_ = false;
            }});
    }
    notify_changed(ChangeField::Visible);
}

void SeriesViewModel::set_color(const Color& c)
{
    if (!model_)
        return;
    model_->color(c);
    notify_changed(ChangeField::Color);
}

void SeriesViewModel::set_label(const std::string& lbl)
{
    if (!model_)
        return;
    if (model_->label() == lbl)
        return;
    std::string old = model_->label();
    model_->label(lbl);
    if (undo_mgr_ && !suppressing_undo_)
    {
        auto* self = this;
        undo_mgr_->push(UndoAction{
            "Rename series",
            [self, old]()
            {
                self->suppressing_undo_ = true;
                self->set_label(old);
                self->suppressing_undo_ = false;
            },
            [self, lbl]()
            {
                self->suppressing_undo_ = true;
                self->set_label(lbl);
                self->suppressing_undo_ = false;
            }});
    }
    notify_changed(ChangeField::Label);
}

void SeriesViewModel::set_opacity(float o)
{
    if (!model_)
        return;
    model_->opacity(o);
    notify_changed(ChangeField::Opacity);
}

// ── Per-view selection / highlight ───────────────────────────────────────

void SeriesViewModel::set_is_selected(bool v)
{
    if (v == is_selected_)
        return;
    is_selected_ = v;
    notify_changed(ChangeField::IsSelected);
}

void SeriesViewModel::set_is_highlighted(bool v)
{
    if (v == is_highlighted_)
        return;
    is_highlighted_ = v;
    notify_changed(ChangeField::IsHighlighted);
}

void SeriesViewModel::notify_changed(ChangeField field)
{
    if (on_changed_)
        on_changed_(*this, field);
}

}   // namespace spectra
