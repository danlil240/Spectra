// window_sdl3_events.cpp — SDL3 event pump for WindowManager.
// Parallel to window_glfw_callbacks.cpp; defines process_sdl3_events()
// and find_by_sdl3_window() as WindowManager member functions.

#ifdef SPECTRA_USE_SDL3

    #include "window_manager.hpp"

    #include <spectra/logger.hpp>

    #include <SDL3/SDL.h>

    #include "render/vulkan/window_context.hpp"
    #include "sdl3_key_map.hpp"
    #include "ui/app/window_ui_context.hpp"

    #ifdef SPECTRA_USE_IMGUI
        #include <imgui.h>
        #include <imgui_impl_sdl3.h>
        #include <imgui_impl_vulkan.h>

        #include "ui/figures/figure_manager.hpp"
        #include "ui/imgui/imgui_integration.hpp"
    #endif

    #include <chrono>

namespace spectra
{

WindowContext* WindowManager::find_by_sdl3_window(SDL_Window* win) const
{
    void* vp = static_cast<void*>(win);
    for (auto* wctx : active_ptrs_)
    {
        if (wctx && wctx->glfw_window == vp)
            return wctx;
    }
    return nullptr;
}

void WindowManager::process_sdl3_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    #ifdef SPECTRA_USE_IMGUI
        // Forward every event to each window's ImGui context so ImGui can
        // consume mouse/keyboard input before we route to our input handlers.
        for (auto* wctx : active_ptrs_)
        {
            if (!wctx || !wctx->ui_ctx || !wctx->ui_ctx->imgui_ui)
                continue;
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGuiContext* wc   = static_cast<ImGuiContext*>(wctx->imgui_context);
            if (wc)
                ImGui::SetCurrentContext(wc);
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (prev && prev != wc)
                ImGui::SetCurrentContext(prev);
        }
    #endif

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (wctx && !wctx->should_close)
                    {
                        wctx->should_close = true;
                        request_close(wctx->id);
                    }
                }
                break;
            }

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.window.windowID)
                    {
                        wctx->should_close = true;
                        request_close(wctx->id);
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.window.windowID)
                    {
                        wctx->is_focused = (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                        if (wctx->is_focused)
                            wctx->z_order = next_z_order_++;
                        request_redraw("focus");
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.window.windowID)
                    {
                        int pw = 0, ph = 0;
                        SDL_GetWindowSizeInPixels(sdl_win, &pw, &ph);
                        if (pw > 0 && ph > 0)
                        {
                            wctx->needs_resize   = true;
                            wctx->pending_width  = static_cast<uint32_t>(pw);
                            wctx->pending_height = static_cast<uint32_t>(ph);
                            wctx->resize_time    = std::chrono::steady_clock::now();
                        }
                        request_redraw("sdl_resize");
                        SPECTRA_LOG_DEBUG("window_manager",
                                          "Window {} resize: {}x{}",
                                          wctx->id,
                                          pw,
                                          ph);
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.motion.windowID)
                    {
    #ifdef SPECTRA_USE_IMGUI
                        if (wctx->ui_ctx)
                        {
                            auto& ui            = *wctx->ui_ctx;
                            auto& input_handler = ui.input_handler;
                            auto& imgui_ui      = ui.imgui_ui;
                            auto& dock_system   = ui.dock_system;

                            bool input_is_dragging =
                                input_handler.mode() == InteractionMode::Dragging
                                || input_handler.is_measure_dragging()
                                || input_handler.is_middle_pan_dragging()
                                || input_handler.has_measure_result();

                            ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                            if (wctx->imgui_context)
                                ImGui::SetCurrentContext(
                                    static_cast<ImGuiContext*>(wctx->imgui_context));

                            if (!input_is_dragging && imgui_ui
                                && (imgui_ui->wants_capture_mouse()
                                    || imgui_ui->is_tab_interacting()))
                            {
                                if (prev_ctx)
                                    ImGui::SetCurrentContext(prev_ctx);
                                request_redraw("sdl_mouse_move");
                                break;
                            }

                            if (dock_system.is_split())
                            {
                                auto* root = dock_system.split_view().root();
                                if (root)
                                {
                                    auto* pane =
                                        root->find_at_point(static_cast<float>(event.motion.x),
                                                            static_cast<float>(event.motion.y));
                                    if (pane && pane->is_leaf() && registry_)
                                    {
                                        auto* pfig = registry_->get(pane->figure_index());
                                        if (pfig)
                                            input_handler.set_figure(pfig);
                                    }
                                }
                            }

                            input_handler.on_mouse_move(event.motion.x, event.motion.y);

                            if (prev_ctx)
                                ImGui::SetCurrentContext(prev_ctx);
                        }
    #else
                        if (wctx->ui_ctx)
                            wctx->ui_ctx->input_handler.on_mouse_move(event.motion.x,
                                                                      event.motion.y);
    #endif
                        request_redraw("sdl_mouse_move");
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                int action = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? 1 : 0;
                int button = sdl3_to_spectra_mouse_button(event.button.button);
                int mods   = sdl3_to_spectra_mods(SDL_GetModState());

                // Callback-based mouse release tracking (tab drag)
                if (mouse_release_tracking_ && button == 0 && action == 0)
                {
                    auto now = std::chrono::steady_clock::now();
                    if (now >= suppress_release_until_)
                        mouse_release_seen_ = true;
                }

                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.button.windowID)
                    {
    #ifdef SPECTRA_USE_IMGUI
                        if (wctx->ui_ctx)
                        {
                            auto& ui            = *wctx->ui_ctx;
                            auto& input_handler = ui.input_handler;
                            auto& imgui_ui      = ui.imgui_ui;
                            auto& dock_system   = ui.dock_system;

                            ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                            if (wctx->imgui_context)
                                ImGui::SetCurrentContext(
                                    static_cast<ImGuiContext*>(wctx->imgui_context));

                            bool input_is_dragging =
                                input_handler.mode() == InteractionMode::Dragging
                                || input_handler.is_measure_dragging()
                                || input_handler.is_middle_pan_dragging();

                            if (!input_is_dragging && imgui_ui
                                && (imgui_ui->wants_capture_mouse()
                                    || imgui_ui->is_tab_interacting()))
                            {
                                if (action == 0)
                                    input_handler.on_mouse_button(button,
                                                                  action,
                                                                  mods,
                                                                  event.button.x,
                                                                  event.button.y);
                                if (prev_ctx)
                                    ImGui::SetCurrentContext(prev_ctx);
                                request_redraw("sdl_mouse_button");
                                break;
                            }

                            if (dock_system.is_split())
                            {
                                auto* root = dock_system.split_view().root();
                                if (root)
                                {
                                    auto* pane =
                                        root->find_at_point(static_cast<float>(event.button.x),
                                                            static_cast<float>(event.button.y));
                                    if (pane && pane->is_leaf() && registry_)
                                    {
                                        auto* pfig = registry_->get(pane->figure_index());
                                        if (pfig)
                                            input_handler.set_figure(pfig);
                                    }
                                }
                            }

                            input_handler.on_mouse_button(button,
                                                          action,
                                                          mods,
                                                          event.button.x,
                                                          event.button.y);

                            if (prev_ctx)
                                ImGui::SetCurrentContext(prev_ctx);
                        }
    #else
                        if (wctx->ui_ctx)
                            wctx->ui_ctx->input_handler.on_mouse_button(button,
                                                                        action,
                                                                        mods,
                                                                        event.button.x,
                                                                        event.button.y);
    #endif
                        request_redraw("sdl_mouse_button");
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                int mods = sdl3_to_spectra_mods(SDL_GetModState());
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.wheel.windowID)
                    {
    #ifdef SPECTRA_USE_IMGUI
                        if (wctx->ui_ctx)
                        {
                            auto& ui            = *wctx->ui_ctx;
                            auto& input_handler = ui.input_handler;
                            auto& imgui_ui      = ui.imgui_ui;
                            auto& cmd_palette   = ui.cmd_palette;

                            ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                            if (wctx->imgui_context)
                                ImGui::SetCurrentContext(
                                    static_cast<ImGuiContext*>(wctx->imgui_context));

                            if (cmd_palette.is_open())
                            {
                                if (prev_ctx)
                                    ImGui::SetCurrentContext(prev_ctx);
                                request_redraw("sdl_scroll");
                                break;
                            }
                            if (imgui_ui && imgui_ui->wants_capture_mouse())
                            {
                                if (prev_ctx)
                                    ImGui::SetCurrentContext(prev_ctx);
                                request_redraw("sdl_scroll");
                                break;
                            }

                            float cx = 0.0f, cy = 0.0f;
                            SDL_GetMouseState(&cx, &cy);

                            auto& dock_system = ui.dock_system;
                            if (dock_system.is_split())
                            {
                                auto* root = dock_system.split_view().root();
                                if (root)
                                {
                                    auto* pane = root->find_at_point(static_cast<float>(cx),
                                                                     static_cast<float>(cy));
                                    if (pane && pane->is_leaf() && registry_)
                                    {
                                        auto* pfig = registry_->get(pane->figure_index());
                                        if (pfig)
                                            input_handler.set_figure(pfig);
                                    }
                                }
                            }

                            input_handler.on_scroll(event.wheel.x, event.wheel.y, cx, cy);

                            if (prev_ctx)
                                ImGui::SetCurrentContext(prev_ctx);
                        }
    #else
                        if (wctx->ui_ctx)
                        {
                            float cx = 0.0f, cy = 0.0f;
                            SDL_GetMouseState(&cx, &cy);
                            wctx->ui_ctx->input_handler.on_scroll(event.wheel.x,
                                                                  event.wheel.y,
                                                                  cx,
                                                                  cy);
                        }
    #endif
                        request_redraw("sdl_scroll");
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                int key    = sdl3_to_spectra_key(event.key.key);
                int action = (event.type == SDL_EVENT_KEY_DOWN) ? 1 : 0;
                int mods   = sdl3_to_spectra_mods(event.key.mod);

                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.key.windowID)
                    {
    #ifdef SPECTRA_USE_IMGUI
                        if (wctx->ui_ctx && key != -1)
                        {
                            ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                            if (wctx->imgui_context)
                                ImGui::SetCurrentContext(
                                    static_cast<ImGuiContext*>(wctx->imgui_context));

                            auto& ui            = *wctx->ui_ctx;
                            auto& input_handler = ui.input_handler;
                            auto& imgui_ui      = ui.imgui_ui;
                            auto& shortcut_mgr  = ui.shortcut_mgr;

                            if (imgui_ui && imgui_ui->wants_capture_keyboard())
                            {
                                if (prev_ctx)
                                    ImGui::SetCurrentContext(prev_ctx);
                                request_redraw("sdl_key");
                                break;
                            }

                            input_handler.on_key(key, action, mods);
                            shortcut_mgr.on_key(key, action, mods);

                            if (prev_ctx)
                                ImGui::SetCurrentContext(prev_ctx);
                        }
    #else
                        if (wctx->ui_ctx && key != -1)
                            wctx->ui_ctx->input_handler.on_key(key, action, mods);
    #endif
                        request_redraw("sdl_key");
                        break;
                    }
                }
                break;
            }

            case SDL_EVENT_TEXT_INPUT:
            {
                for (auto* wctx : active_ptrs_)
                {
                    if (!wctx || !wctx->glfw_window)
                        continue;
                    auto* sdl_win = static_cast<SDL_Window*>(wctx->glfw_window);
                    if (SDL_GetWindowID(sdl_win) == event.text.windowID)
                    {
                        // ImGui_ImplSDL3_ProcessEvent already handled this above.
                        request_redraw("sdl_text");
                        break;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_SDL3
