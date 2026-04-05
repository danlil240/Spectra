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

#include <chrono>

namespace spectra
{

std::vector<AutomationHandlerEntry> make_window_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

    // ── resize_window ────────────────────────────────────────────────────
    entries.push_back({"resize_window",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#ifdef SPECTRA_USE_GLFW
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           uint32_t w =
                               static_cast<uint32_t>(json_get_int(req.params_json, "width", 1280));
                           uint32_t h =
                               static_cast<uint32_t>(json_get_int(req.params_json, "height", 720));
                           ui_ctx->needs_resize          = true;
                           ui_ctx->new_width             = w;
                           ui_ctx->new_height            = h;
                           ui_ctx->resize_requested_time = std::chrono::steady_clock::now();
                           if (ui_ctx->glfw_window)
                               glfwSetWindowSize(static_cast<GLFWwindow*>(ui_ctx->glfw_window),
                                                 static_cast<int>(w),
                                                 static_cast<int>(h));
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "GLFW not available");
#endif
                       }});

    // ── get_window_size ──────────────────────────────────────────────────
    entries.push_back({"get_window_size",
                       [](AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
                       {
#ifdef SPECTRA_USE_GLFW
                           if (!ui_ctx)
                           {
                               req.response_json = json_error(req.id, "No UI context");
                               return;
                           }
                           Backend* backend = app.backend();
                           if (!backend)
                           {
                               req.response_json = json_error(req.id, "No backend available");
                               return;
                           }
                           uint32_t w = backend->swapchain_width();
                           uint32_t h = backend->swapchain_height();
                           req.response_json =
                               json_ok(req.id,
                                       "{\"width\":" + std::to_string(w)
                                           + ",\"height\":" + std::to_string(h) + "}");
#else
                           (void)ui_ctx;
                           (void)app;
                           req.response_json = json_error(req.id, "GLFW not available");
#endif
                       }});

    return entries;
}

}   // namespace spectra
