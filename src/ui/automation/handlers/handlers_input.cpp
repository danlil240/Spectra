// handlers_input.cpp — mouse, keyboard, scroll, text input handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include <spectra/app.hpp>

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/input/input.hpp"
#endif
#ifdef SPECTRA_USE_GLFW
    #include <GLFW/glfw3.h>
#endif
#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
    #include "ui/imgui/imgui_integration.hpp"
#endif

namespace spectra
{

std::vector<AutomationHandlerEntry> make_input_handlers()
{
    using Ctx = AutomationContextFlag;
    const Ctx kWindowUi = Ctx::UiContext | Ctx::Windowing;
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "mouse_move",
        "Move the mouse cursor to the specified position.",
        kWindowUi,
        {{.name = "x", .kind = ParamKind::Number, .required = true},
         {.name = "y", .kind = ParamKind::Number, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
        {
            double x = json_get_number(req.params_json, "x");
            double y = json_get_number(req.params_json, "y");
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx->imgui_ui)
            {
                ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                ImGuiContext* win_ctx  = ui_ctx->imgui_ui->imgui_context();
                if (win_ctx)
                    ImGui::SetCurrentContext(win_ctx);
                ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
                if (win_ctx && prev_ctx != win_ctx)
                    ImGui::SetCurrentContext(prev_ctx);
            }
#endif
            ui_ctx->input_handler.on_mouse_move(x, y);
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "mouse_click",
        "Click at the specified position.",
        kWindowUi,
        {{.name = "x", .kind = ParamKind::Number, .required = true},
         {.name = "y", .kind = ParamKind::Number, .required = true}},
        [](AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
        {
            double x   = json_get_number(req.params_json, "x");
            double y   = json_get_number(req.params_json, "y");
            int    btn = json_get_int(req.params_json, "button", 0);
            int    mod = json_get_int(req.params_json, "modifiers", 0);
#ifdef SPECTRA_USE_GLFW
            if (auto* win = static_cast<GLFWwindow*>(ui_ctx->glfw_window))
                glfwSetCursorPos(win, x, y);
#elif defined(SPECTRA_USE_SDL3)
            if (auto* win = static_cast<SDL_Window*>(ui_ctx->glfw_window))
                SDL_WarpMouseInWindow(win, static_cast<float>(x), static_cast<float>(y));
#endif
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx->imgui_ui)
            {
                ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                ImGuiContext* win_ctx  = ui_ctx->imgui_ui->imgui_context();
                if (win_ctx)
                    ImGui::SetCurrentContext(win_ctx);
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
                io.AddMouseButtonEvent(btn, true);
                io.AddMouseButtonEvent(btn, false);
                if (win_ctx && prev_ctx != win_ctx)
                    ImGui::SetCurrentContext(prev_ctx);
            }
#endif
            ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
            ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
            if (auto* sess = app.session())
                sess->redraw_tracker().mark_dirty("mouse_click");
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "mouse_drag",
        "Drag the mouse from one position to another.",
        kWindowUi,
        {{.name = "x1", .kind = ParamKind::Number, .required = true},
         {.name = "y1", .kind = ParamKind::Number, .required = true},
         {.name = "x2", .kind = ParamKind::Number, .required = true},
         {.name = "y2", .kind = ParamKind::Number, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
        {
            double x1    = json_get_number(req.params_json, "x1");
            double y1    = json_get_number(req.params_json, "y1");
            double x2    = json_get_number(req.params_json, "x2");
            double y2    = json_get_number(req.params_json, "y2");
            int    btn   = json_get_int(req.params_json, "button", 0);
            int    mod   = json_get_int(req.params_json, "modifiers", 0);
            int    steps = json_get_int(req.params_json, "steps", 10);
            if (steps < 2)
                steps = 2;

            ui_ctx->input_handler.on_mouse_move(x1, y1);
            ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x1, y1);
            for (int i = 1; i <= steps; ++i)
            {
                double t  = static_cast<double>(i) / steps;
                double mx = x1 + (x2 - x1) * t;
                double my = y1 + (y2 - y1) * t;
                ui_ctx->input_handler.on_mouse_move(mx, my);
            }
            ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x2, y2);
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "scroll",
        "Scroll at the specified position.",
        kWindowUi,
        {{.name = "x", .kind = ParamKind::Number, .required = true},
         {.name = "y", .kind = ParamKind::Number, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
        {
            double x  = json_get_number(req.params_json, "x");
            double y  = json_get_number(req.params_json, "y");
            double dx = json_get_number(req.params_json, "dx", 0.0);
            double dy = json_get_number(req.params_json, "dy", 1.0);
            ui_ctx->input_handler.on_scroll(x, y, dx, dy);
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "key_press",
        "Press and release a keyboard key.",
        kWindowUi,
        {{.name = "key", .kind = ParamKind::Int, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
        {
            int key = json_get_int(req.params_json, "key");
            int mod = json_get_int(req.params_json, "modifiers", 0);
            ui_ctx->input_handler.on_key(key, 1, mod);
            ui_ctx->input_handler.on_key(key, 0, mod);
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "text_input",
        "Inject text into the active ImGui text field.",
        Ctx::ImGui,
        {{.name = "text", .kind = ParamKind::String, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* /*ui_ctx*/)
        {
#ifdef SPECTRA_USE_IMGUI
            std::string text = json_get_string(req.params_json, "text");
            auto&       io   = ImGui::GetIO();
            for (char c : text)
                io.AddInputCharacter(static_cast<unsigned int>(c));
            req.response_json =
                json_ok(req.id, "{\"chars\":" + std::to_string(text.size()) + "}");
#else
            req.response_json = json_error(req.id, "ImGui not available");
#endif
        }));

    entries.push_back(automation_handler(
        "double_click",
        "Double-click at the specified position.",
        kWindowUi,
        {{.name = "x", .kind = ParamKind::Number, .required = true},
         {.name = "y", .kind = ParamKind::Number, .required = true}},
        [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
        {
            double x   = json_get_number(req.params_json, "x");
            double y   = json_get_number(req.params_json, "y");
            int    btn = json_get_int(req.params_json, "button", 0);
            int    mod = json_get_int(req.params_json, "modifiers", 0);
            ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
            ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
            ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
            ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
            req.response_json = json_ok(req.id);
        }));

    return entries;
}

}   // namespace spectra
