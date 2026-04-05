// handlers_utility.cpp — ping, pump_frames, wait_frames handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

namespace spectra
{

std::vector<AutomationHandlerEntry> make_utility_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

    // ── pump_frames ──────────────────────────────────────────────────────
    entries.push_back({"pump_frames",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* /*ui_ctx*/)
                       {
                           int count = json_get_int(req.params_json, "count", 1);
                           if (count < 1)
                               count = 1;
                           if (count > 600)
                               count = 600;
                           req.response_json =
                               json_ok(req.id, "{\"pumped\":" + std::to_string(count) + "}");
                       }});

    // ── wait_frames ──────────────────────────────────────────────────────
    // Normally intercepted in poll(), but provide a fallback.
    entries.push_back({"wait_frames",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* /*ui_ctx*/)
                       { req.response_json = json_ok(req.id, "{\"waited\":true}"); }});

    // ── ping ─────────────────────────────────────────────────────────────
    entries.push_back({"ping",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* /*ui_ctx*/)
                       { req.response_json = json_ok(req.id, "{\"pong\":true}"); }});

    return entries;
}

}   // namespace spectra
