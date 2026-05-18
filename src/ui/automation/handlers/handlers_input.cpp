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
    std::vector<AutomationHandlerEntry> entries;

    // ── mouse_move ───────────────────────────────────────────────────────
    entries.push_back({"mouse_move",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           double x = json_get_number(req.params_json, "x");
                           double y = json_get_number(req.params_json, "y");
    #ifdef SPECTRA_USE_IMGUI
                           if (ui_ctx->imgui_ui)
                           {
                               ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                               ImGuiContext* win_ctx  = ui_ctx->imgui_ui->imgui_context();
                               if (win_ctx)
                                   ImGui::SetCurrentContext(win_ctx);
                               ImGui::GetIO().AddMousePosEvent(static_cast<float>(x),
                                                               static_cast<float>(y));
                               if (win_ctx && prev_ctx != win_ctx)
                                   ImGui::SetCurrentContext(prev_ctx);
                           }
    #endif
                           ui_ctx->input_handler.on_mouse_move(x, y);
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    // ── mouse_click ──────────────────────────────────────────────────────
    entries.push_back({"mouse_click",
                       [](AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           double x   = json_get_number(req.params_json, "x");
                           double y   = json_get_number(req.params_json, "y");
                           int    btn = json_get_int(req.params_json, "button", 0);
                           int    mod = json_get_int(req.params_json, "modifiers", 0);
        // Move the real cursor so events don't override our injected position.
    #ifdef SPECTRA_USE_GLFW
                           if (auto* win = static_cast<GLFWwindow*>(ui_ctx->glfw_window))
                               glfwSetCursorPos(win, x, y);
    #elif defined(SPECTRA_USE_SDL3)
                           if (auto* win = static_cast<SDL_Window*>(ui_ctx->glfw_window))
                               SDL_WarpMouseInWindow(win,
                                                     static_cast<float>(x),
                                                     static_cast<float>(y));
    #endif
    #ifdef SPECTRA_USE_IMGUI
                           // Inject into ImGui IO for widget clicks (tabs, buttons, etc.).
                           // Must switch to this window's own ImGui context — Spectra uses
                           // a separate ImGuiContext per window, and multiple windows may be
                           // active simultaneously (e.g. preview window).  Injecting into
                           // the wrong context silently drops the events.
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
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    // ── mouse_drag ───────────────────────────────────────────────────────
    entries.push_back({"mouse_drag",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
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
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    // ── scroll ───────────────────────────────────────────────────────────
    entries.push_back({"scroll",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           double x  = json_get_number(req.params_json, "x");
                           double y  = json_get_number(req.params_json, "y");
                           double dx = json_get_number(req.params_json, "dx", 0.0);
                           double dy = json_get_number(req.params_json, "dy", 1.0);
                           ui_ctx->input_handler.on_scroll(x, y, dx, dy);
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    // ── key_press ────────────────────────────────────────────────────────
    entries.push_back({"key_press",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           int key = json_get_int(req.params_json, "key");
                           int mod = json_get_int(req.params_json, "modifiers", 0);
                           ui_ctx->input_handler.on_key(key, 1, mod);
                           ui_ctx->input_handler.on_key(key, 0, mod);
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    // ── text_input ───────────────────────────────────────────────────────
    entries.push_back({"text_input",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* /*ui_ctx*/)
                       {
#ifdef SPECTRA_USE_IMGUI
                           std::string text = json_get_string(req.params_json, "text");
                           if (text.empty())
                           {
                               req.response_json = json_error(req.id, "Missing text");
                               return;
                           }
                           auto& io = ImGui::GetIO();
                           for (char c : text)
                               io.AddInputCharacter(static_cast<unsigned int>(c));
                           req.response_json =
                               json_ok(req.id, "{\"chars\":" + std::to_string(text.size()) + "}");
#else
                           req.response_json = json_error(req.id, "ImGui not available");
#endif
                       }});

    // ── double_click ─────────────────────────────────────────────────────
    entries.push_back({"double_click",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           double x   = json_get_number(req.params_json, "x");
                           double y   = json_get_number(req.params_json, "y");
                           int    btn = json_get_int(req.params_json, "button", 0);
                           int    mod = json_get_int(req.params_json, "modifiers", 0);
                           // First click
                           ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
                           ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
                           // Second click (double)
                           ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
                           ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "No windowing backend");
#endif
                       }});

    return entries;
}

}   // namespace spectra
