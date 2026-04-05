// commands_app.cpp — App-level and window command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include "ui/app/ros2_adapter_state.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include "render/vulkan/window_context.hpp"
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_ROS2
    #ifdef __unix__
        #include <sys/types.h>
        #include <unistd.h>
    #endif
    #include <cstdlib>
    #include <thread>
#endif

#include <algorithm>
#include <string>

namespace spectra
{

std::vector<CommandDescriptor> make_app_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& ui_ctx      = ctx.ui_ctx;
    auto& cmd_palette = ui_ctx.cmd_palette;

    cmds.push_back({"app.command_palette",
                    "Command Palette",
                    "Ctrl+K",
                    "App",
                    static_cast<uint16_t>(ui::Icon::Search),
                    [&]() { cmd_palette.toggle(); }});

    cmds.push_back({"app.cancel",
                    "Cancel / Close",
                    "Escape",
                    "App",
                    0,
                    [&]()
                    {
                        if (cmd_palette.is_open())
                        {
                            cmd_palette.close();
                        }
                    }});

    #ifdef SPECTRA_USE_GLFW
    {
        auto&  registry         = ctx.registry;
        auto&  active_figure_id = *ctx.active_figure_id;
        auto*& active_figure    = *ctx.active_figure;
        auto&  fig_mgr          = *ui_ctx.fig_mgr;
        auto*  window_mgr       = ctx.window_mgr;

        cmds.push_back({"app.new_window",
                        "New Window",
                        "Ctrl+Shift+N",
                        "App",
                        static_cast<uint16_t>(ui::Icon::Plus),
                        [&, window_mgr]()
                        {
                            if (!window_mgr)
                                return;
                            FigureId dup_id = fig_mgr.duplicate_figure(active_figure_id);
                            if (dup_id == INVALID_FIGURE_ID)
                                return;
                            Figure*     dup_fig   = registry.get(dup_id);
                            uint32_t    w         = dup_fig ? dup_fig->width() : 800;
                            uint32_t    h         = dup_fig ? dup_fig->height() : 600;
                            std::string win_title = fig_mgr.get_title(dup_id);
                            window_mgr->create_window_with_ui(w, h, win_title, dup_id);
                        }});

        cmds.push_back(
            {"figure.move_to_window",
             "Move Figure to Window",
             "Ctrl+Shift+M",
             "App",
             static_cast<uint16_t>(ui::Icon::Plus),
             [&, window_mgr]()
             {
                 if (!window_mgr)
                     return;
                 if (!window_mgr || window_mgr->windows().empty())
                     return;
                 auto* src_wctx = window_mgr->focused_window();
                 if (!src_wctx)
                     src_wctx = window_mgr->windows()[0];

                 FigureId fig_id = active_figure_id;
                 if (fig_id == INVALID_FIGURE_ID)
                     return;

                 if (fig_mgr.count() <= 1)
                 {
                     SPECTRA_LOG_WARN("window_manager", "Cannot move last figure from window");
                     return;
                 }

                 WindowContext* target = nullptr;
                 for (auto* wctx : window_mgr->windows())
                 {
                     if (wctx != src_wctx && wctx->ui_ctx)
                     {
                         target = wctx;
                         break;
                     }
                 }

                 if (target)
                 {
                     window_mgr->move_figure(fig_id, src_wctx->id, target->id);
                 }
                 else
                 {
                     Figure*     fig   = registry.get(fig_id);
                     uint32_t    w     = fig ? fig->width() : 800;
                     uint32_t    h     = fig ? fig->height() : 600;
                     std::string title = fig_mgr.get_title(fig_id);

                     FigureState state = fig_mgr.remove_figure(fig_id);

                     auto& pf = src_wctx->assigned_figures;
                     pf.erase(std::remove(pf.begin(), pf.end(), fig_id), pf.end());
                     if (src_wctx->active_figure_id == fig_id)
                         src_wctx->active_figure_id = pf.empty() ? INVALID_FIGURE_ID : pf.front();

                     auto* new_wctx = window_mgr->create_window_with_ui(w, h, title, fig_id);
                     if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                     {
                         auto* new_fm              = new_wctx->ui_ctx->fig_mgr;
                         new_fm->state(fig_id)     = std::move(state);
                         std::string correct_title = new_fm->get_title(fig_id);
                         if (new_fm->tab_bar())
                             new_fm->tab_bar()->set_tab_title(0, correct_title);
                     }
                 }
             }});
    }
    #endif   // SPECTRA_USE_GLFW

    // ─── ROS2 Adapter command ────────────────────────────────────────────
    #ifdef SPECTRA_USE_ROS2
    cmds.push_back(
        {"tools.ros2_adapter",
         "ROS2 Adapter",
         "",
         "Tools",
         static_cast<uint16_t>(ui::Icon::Wrench),
         []()
         {
        #ifdef __unix__
             pid_t pid = fork();
             if (pid == 0)
             {
                 execlp("spectra-ros", "spectra-ros", static_cast<char*>(nullptr));
                 _exit(127);
             }
             else if (pid > 0)
             {
                 SPECTRA_LOG_INFO("ros2_adapter",
                                  "Launched spectra-ros (pid=" + std::to_string(pid) + ")");
             }
             else
             {
                 SPECTRA_LOG_ERROR("ros2_adapter", "fork() failed — cannot launch spectra-ros");
                 spectra::ros2_adapter_set_error("Failed to fork() a child process.\n"
                                                 "Cannot launch spectra-ros.");
             }
        #elif defined(_WIN32)
             std::thread([]() { std::system("start spectra-ros"); }).detach();
        #endif
         }});
    #endif   // SPECTRA_USE_ROS2
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
