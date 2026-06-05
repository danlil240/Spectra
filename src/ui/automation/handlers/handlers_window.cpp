// handlers_window.cpp — resize_window, get_window_size handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>

#include "render/backend.hpp"
#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#endif
#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
#endif

#include <chrono>

namespace spectra
{

std::vector<AutomationHandlerEntry> make_window_handlers()
{
    using Ctx = AutomationContextFlag;
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "resize_window",
        "Resize the application window.",
        Ctx::UiContext | Ctx::Windowing,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
        {
            uint32_t w = static_cast<uint32_t>(json_get_int(req.params_json, "width", 1280));
            uint32_t h = static_cast<uint32_t>(json_get_int(req.params_json, "height", 720));
            ui_ctx->needs_resize          = true;
            ui_ctx->new_width             = w;
            ui_ctx->new_height            = h;
            ui_ctx->resize_requested_time = std::chrono::steady_clock::now();
#ifdef SPECTRA_USE_GLFW
            if (ui_ctx->glfw_window)
                glfwSetWindowSize(static_cast<GLFWwindow*>(ui_ctx->glfw_window),
                                  static_cast<int>(w),
                                  static_cast<int>(h));
#elif defined(SPECTRA_USE_SDL3)
            if (ui_ctx->glfw_window)
                SDL_SetWindowSize(static_cast<SDL_Window*>(ui_ctx->glfw_window),
                                  static_cast<int>(w),
                                  static_cast<int>(h));
#endif
            req.response_json = json_ok(req.id);
        }));

    entries.push_back(automation_handler(
        "get_window_size",
        "Get the current window/swapchain size.",
        Ctx::UiContext | Ctx::Windowing | Ctx::Backend,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
        {
            Backend* backend  = app->backend();
            uint32_t w        = backend->swapchain_width();
            uint32_t h        = backend->swapchain_height();
            req.response_json = json_ok(
                req.id,
                "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "}");
        }));

    return entries;
}

}   // namespace spectra
