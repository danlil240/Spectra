// handlers_utility.cpp — ping, pump_frames, wait_frames handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

namespace spectra
{

std::vector<AutomationHandlerEntry> make_utility_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "pump_frames",
        "Advance the application by rendering the specified number of frames.",
        AutomationContextFlag::None,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        {
            int count = json_get_int(req.params_json, "count", 1);
            if (count < 1)
                count = 1;
            if (count > 600)
                count = 600;
            req.response_json = json_ok(req.id, "{\"pumped\":" + std::to_string(count) + "}");
        }));

    entries.push_back(
        automation_handler("wait_frames",
                           "Defer the response until N frames have elapsed.",
                           AutomationContextFlag::None,
                           {},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_ok(req.id, "{\"waited\":true}"); }));

    entries.push_back(
        automation_handler("ping",
                           "Ping the Spectra application to verify the connection is alive.",
                           AutomationContextFlag::None,
                           {},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_ok(req.id, "{\"pong\":true}"); }));

    return entries;
}

}   // namespace spectra
