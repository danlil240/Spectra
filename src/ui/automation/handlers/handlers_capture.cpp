// handlers_capture.cpp — capture_screenshot, capture_window,
//                        get_screenshot_base64 handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include "render/backend.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

namespace spectra
{

std::vector<AutomationHandlerEntry> make_capture_handlers()
{
    using Ctx = AutomationContextFlag;
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "capture_screenshot",
        "Capture the active figure as a PNG file.",
        Ctx::None,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
        {
            std::string path = json_get_string(req.params_json, "path");
            if (path.empty())
                path = "/tmp/spectra_auto_screenshot.png";

            FigureId active_id = INVALID_FIGURE_ID;
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx && ui_ctx->fig_mgr)
                active_id = ui_ctx->fig_mgr->active_index();
#else
            (void)ui_ctx;
#endif
            Figure* fig = app->figure_registry().get(active_id);
            if (!fig)
            {
                auto ids = app->figure_registry().all_ids();
                if (!ids.empty())
                    fig = app->figure_registry().get(ids[0]);
            }
            if (fig)
            {
                fig->save_png(path);
                req.response_json = json_ok(req.id, R"({"path":")" + json_escape(path) + "\"}");
            }
            else
            {
                req.response_json = json_error(req.id, "No figure to capture");
            }
        }));

    entries.push_back(automation_handler(
        "capture_window",
        "Capture the full window framebuffer as a PNG file.",
        Ctx::Backend,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
        {
            std::string path = json_get_string(req.params_json, "path");
            if (path.empty())
                path = "/tmp/spectra_auto_window.png";

            Backend* backend = app->backend();
            uint32_t w       = backend->swapchain_width();
            uint32_t h       = backend->swapchain_height();
            if (w == 0 || h == 0)
            {
                req.response_json = json_error(req.id, "Swapchain not ready");
                return;
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
            if (!backend->readback_framebuffer(pixels.data(), w, h))
            {
                req.response_json = json_error(req.id, "Framebuffer readback failed");
                return;
            }

            if (!ImageExporter::write_png(path, pixels.data(), w, h))
            {
                req.response_json = json_error(req.id, "PNG write failed");
                return;
            }

            SPECTRA_LOG_INFO("automation", "Window screenshot saved: " + path);
            req.response_json =
                json_ok(req.id,
                        R"({"path":")" + json_escape(path) + R"(","width":)" + std::to_string(w)
                            + ",\"height\":" + std::to_string(h) + "}");
        }));

    entries.push_back(automation_handler(
        "get_screenshot_base64",
        "Capture the window framebuffer and return PNG data as base64.",
        Ctx::Backend,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
        {
            Backend* backend = app->backend();
            uint32_t w       = backend->swapchain_width();
            uint32_t h       = backend->swapchain_height();
            if (w == 0 || h == 0)
            {
                req.response_json = json_error(req.id, "Swapchain not ready");
                return;
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
            if (!backend->readback_framebuffer(pixels.data(), w, h))
            {
                req.response_json = json_error(req.id, "Framebuffer readback failed");
                return;
            }

            auto png_data = ImageExporter::write_png_to_memory(pixels.data(), w, h);
            if (png_data.empty())
            {
                req.response_json = json_error(req.id, "PNG encoding failed");
                return;
            }

            static constexpr const char* kB64 =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string b64;
            b64.reserve((png_data.size() + 2) / 3 * 4);
            for (size_t i = 0; i < png_data.size(); i += 3)
            {
                uint32_t n = static_cast<uint32_t>(png_data[i]) << 16;
                if (i + 1 < png_data.size())
                    n |= static_cast<uint32_t>(png_data[i + 1]) << 8;
                if (i + 2 < png_data.size())
                    n |= static_cast<uint32_t>(png_data[i + 2]);
                b64 += kB64[(n >> 18) & 0x3F];
                b64 += kB64[(n >> 12) & 0x3F];
                b64 += (i + 1 < png_data.size()) ? kB64[(n >> 6) & 0x3F] : '=';
                b64 += (i + 2 < png_data.size()) ? kB64[n & 0x3F] : '=';
            }

            req.response_json =
                json_ok(req.id,
                        "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h)
                            + R"(,"format":"png","data":")" + b64 + "\"}");
        }));

    return entries;
}

}   // namespace spectra
