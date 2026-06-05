// handlers_command.cpp — execute_command, list_commands handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/commands/command_registry.hpp"

#include <sstream>

namespace spectra
{

std::vector<AutomationHandlerEntry> make_command_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

#ifdef SPECTRA_USE_IMGUI
    entries.push_back(automation_handler(
        "execute_command",
        "Execute a registered Spectra UI command by its ID.",
        AutomationContextFlag::ImGui,
        {{.name = "command_id", .kind = ParamKind::String, .required = true}},
        [](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
        {
            std::string cmd_id = json_get_string(req.params_json, "command_id");
            bool        ok     = ui_ctx->cmd_registry.execute(cmd_id);
            if (ok)
            {
                if (auto* sess = app->session())
                    sess->redraw_tracker().mark_dirty("execute_command");
            }
            req.response_json =
                ok ? json_ok(req.id, "{\"executed\":\"" + json_escape(cmd_id) + "\"}")
                   : json_error(req.id, "Command not found or disabled: " + cmd_id);
        }));

    entries.push_back(automation_handler(
        "list_commands",
        "List all registered UI commands in the Spectra application.",
        AutomationContextFlag::ImGui,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
        {
            auto               all = ui_ctx->cmd_registry.all_commands();
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (i > 0)
                    oss << ",";
                oss << "{\"id\":\"" << json_escape(all[i]->id) << "\",\"label\":\""
                    << json_escape(all[i]->label) << "\",\"category\":\""
                    << json_escape(all[i]->category) << "\",\"shortcut\":\""
                    << json_escape(all[i]->shortcut)
                    << "\",\"enabled\":" << (all[i]->enabled ? "true" : "false") << "}";
            }
            oss << "]";
            req.response_json = json_ok(req.id, "{\"commands\":" + oss.str() + "}");
        }));

#else
    entries.push_back(automation_handler(
        "execute_command",
        "Execute a registered Spectra UI command by its ID.",
        AutomationContextFlag::ImGui,
        {{.name = "command_id", .kind = ParamKind::String, .required = true}},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        { req.response_json = json_error(req.id, "ImGui not available"); }));

    entries.push_back(automation_handler(
        "list_commands",
        "List all registered UI commands in the Spectra application.",
        AutomationContextFlag::ImGui,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        { req.response_json = json_error(req.id, "ImGui not available"); }));
#endif

    return entries;
}

}   // namespace spectra
