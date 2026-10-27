#include "axes_context_menu_model.hpp"

#include "ui/commands/command_registry.hpp"

namespace spectra
{

namespace
{

class MenuBuilder
{
   public:
    explicit MenuBuilder(const CommandRegistry* registry) : registry_(registry) {}

    void separator()
    {
        if (!items_.empty() && items_.back().kind != AxesMenuItemKind::Separator)
            items_.push_back({AxesMenuItemKind::Separator, {}, {}, {}, false});
    }

    // Adds a registry command; `label` overrides the registry label when the
    // generic command name reads oddly in an axes-scoped menu.
    void command(const char* id, const char* label = nullptr)
    {
        const Command* cmd = registry_ ? registry_->find(id) : nullptr;
        if (!cmd)
            return;
        items_.push_back(
            {AxesMenuItemKind::Command, label ? label : cmd->label, cmd->shortcut, id, false});
    }

    void action(AxesMenuItemKind kind,
                std::string      label,
                const char*      shortcut_from = nullptr,
                bool             danger        = false)
    {
        std::string shortcut;
        if (shortcut_from && registry_)
        {
            if (const Command* cmd = registry_->find(shortcut_from))
                shortcut = cmd->shortcut;
        }
        items_.push_back({kind, std::move(label), std::move(shortcut), {}, danger});
    }

    std::vector<AxesMenuItem> finish()
    {
        if (!items_.empty() && items_.back().kind == AxesMenuItemKind::Separator)
            items_.pop_back();
        return std::move(items_);
    }

   private:
    const CommandRegistry*    registry_;
    std::vector<AxesMenuItem> items_;
};

std::string counted(const char* verb, size_t n)
{
    if (n > 1)
        return std::string(verb) + " " + std::to_string(n) + " Series";
    return std::string(verb) + " Series";
}

}   // namespace

std::vector<AxesMenuItem> build_axes_context_menu(const AxesContextMenuState& state,
                                                  const CommandRegistry*      registry)
{
    MenuBuilder m(registry);

    // ── Series under the cursor / selection ──────────────────────────────
    if (state.has_series_clipboard)
    {
        if (state.selected_series > 0)
        {
            m.action(AxesMenuItemKind::CopySeries,
                     counted("Copy", state.selected_series),
                     "series.copy");
            m.action(AxesMenuItemKind::CutSeries,
                     counted("Cut", state.selected_series),
                     "series.cut");
            m.action(AxesMenuItemKind::DeleteSeries,
                     counted("Delete", state.selected_series),
                     "series.delete",
                     true);
        }
        if (state.clipboard_series > 0)
        {
            m.action(AxesMenuItemKind::PasteSeries,
                     counted("Paste", state.clipboard_series),
                     "series.paste");
        }
        m.separator();
    }

    // ── View ─────────────────────────────────────────────────────────────
    m.command("view.autofit", "Autoscale Axes");
    m.command("view.reset");
    m.command("view.toggle_grid");
    if (!state.is_3d)
    {
        m.command("view.toggle_legend");
        m.command("view.toggle_crosshair");
    }
    m.separator();

    // ── Axis linking (only meaningful with several axes) ─────────────────
    if (state.has_multiple_axes)
    {
        if (state.is_3d)
        {
            m.action(AxesMenuItemKind::Link3D, "Link 3D Axes (XYZ)");
        }
        else
        {
            m.action(AxesMenuItemKind::LinkX, "Link X-Axis");
            m.action(AxesMenuItemKind::LinkY, "Link Y-Axis");
            m.action(AxesMenuItemKind::LinkBoth, "Link Both Axes");
        }
        if (state.is_linked)
        {
            m.separator();
            m.action(AxesMenuItemKind::LinkGroupsInfo, "");
            m.action(AxesMenuItemKind::UnlinkThis, "Unlink This Subplot", nullptr, true);
        }
        m.action(AxesMenuItemKind::UnlinkAll, "Unlink All", nullptr, true);
        m.separator();
    }

    // ── Image / export ───────────────────────────────────────────────────
    m.command("file.copy_to_clipboard", "Copy Image");
    m.command("file.export_png", "Export PNG...");
    if (!state.is_3d)
        m.command("file.export_svg", "Export SVG...");
    m.separator();

    // ── Properties ───────────────────────────────────────────────────────
    if (!state.is_3d)
        m.action(AxesMenuItemKind::AxesProperties, "Axes Properties...");

    return m.finish();
}

}   // namespace spectra
