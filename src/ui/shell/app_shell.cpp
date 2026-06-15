#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/app_shell.hpp"

    #include "imgui.h"
    #include "ui/layout/layout_manager.hpp"

namespace spectra::ui::shell
{
AppShell::AppShell(AppShellConfig cfg)
    : config_(std::move(cfg)), canvas_host_(std::make_unique<CanvasHost>(nullptr))
{
}

AppShell::~AppShell() = default;

void AppShell::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
    status_bar_.set_layout_manager(lm);
    if (canvas_host_)
        canvas_host_->set_layout_manager(lm);
}

spectra::LayoutManager* AppShell::layout_manager() const
{
    return layout_manager_;
}

void AppShell::initialize()
{
    if (initialized_)
        return;

    on_register_panels();
    nav_rail_.set_registry(&panels_);
    on_populate_nav_rail(nav_rail_);
    on_populate_menus(menu_bar_);
    menu_bar_.bind_panel_registry(panels_);
    on_build_status_bar(status_bar_);
    initialized_ = true;
}

void AppShell::draw_frame()
{
    if (config_.menu_bar)
        menu_bar_.draw();

    if (config_.nav_rail)
        nav_rail_.draw();

    if (config_.dockspace)
        draw_dockspace();
    else if (canvas_host_)
        canvas_host_->draw();

    panels_.draw_all();

    if (config_.status_bar)
        status_bar_.draw();

    on_draw_canvas();
}

PanelRegistry& AppShell::panels()
{
    return panels_;
}

NavRail& AppShell::nav_rail()
{
    return nav_rail_;
}

MenuBar& AppShell::menu_bar()
{
    return menu_bar_;
}

StatusBar& AppShell::status_bar()
{
    return status_bar_;
}

CanvasHost& AppShell::canvas_host()
{
    return *canvas_host_;
}

void AppShell::draw_dockspace()
{
    #ifdef IMGUI_HAS_DOCK
    Rect canvas{};
    if (layout_manager_)
    {
        canvas = layout_manager_->canvas_rect();
    }
    else
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        canvas.x                = vp->WorkPos.x;
        canvas.y                = vp->WorkPos.y;
        canvas.w                = vp->WorkSize.x;
        canvas.h                = vp->WorkSize.y;
    }

    ImGui::SetNextWindowPos(ImVec2(canvas.x, canvas.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(160.0f, canvas.w), std::max(120.0f, canvas.h)),
                             ImGuiCond_Always);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                  | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                                  | ImGuiWindowFlags_NoDocking;

    const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##AppShellDockHost", nullptr, host_flags))
    {
        dockspace_id_ = ImGui::GetID("##AppShellDockSpace");

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), dock_flags);
        ImGui::PopStyleColor();

        if (!dock_layout_initialized_)
        {
            on_default_layout(dockspace_id_);
            dock_layout_initialized_ = true;
        }

        if (canvas_host_)
            canvas_host_->draw();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    #else
    dock_layout_initialized_ = true;
    if (canvas_host_)
        canvas_host_->draw();
    #endif
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
