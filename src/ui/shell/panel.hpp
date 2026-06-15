#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <string>
    #include <utility>
    #include "ui/theme/icons.hpp"

namespace spectra::ui::shell
{
enum class DockSlot
{
    Center,
    Left,
    Right,
    Bottom,
    Floating
};

struct PanelInfo
{
    std::string id;
    std::string title;
    Icon        icon            = Icon::WindowIcon;
    std::string category        = "General";
    DockSlot    slot            = DockSlot::Floating;
    bool        closable        = true;
    bool        default_visible = false;
};

class Panel
{
   public:
    explicit Panel(PanelInfo info) : info_(std::move(info)), visible_(info_.default_visible) {}
    virtual ~Panel() = default;

    const std::string& id() const { return info_.id; }
    const std::string& title() const { return info_.title; }
    Icon               icon() const { return info_.icon; }
    const std::string& category() const { return info_.category; }
    DockSlot           default_slot() const { return info_.slot; }
    bool               closable() const { return info_.closable; }

    bool  visible() const { return visible_; }
    void  set_visible(bool v) { visible_ = v; }
    bool* visible_ptr() { return &visible_; }

    virtual void draw() = 0;

   protected:
    PanelInfo info_;
    bool      visible_ = false;
};

class CallbackPanel final : public Panel
{
   public:
    using DrawFn = std::function<void(bool* p_open)>;
    CallbackPanel(PanelInfo info, DrawFn fn) : Panel(std::move(info)), fn_(std::move(fn)) {}
    void draw() override
    {
        if (fn_)
            fn_(closable() ? &visible_ : nullptr);
    }

   private:
    DrawFn fn_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
