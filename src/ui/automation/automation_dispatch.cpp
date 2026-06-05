// automation_dispatch.cpp — Context checks, param validation, handler wrapping.

#include "automation_dispatch.hpp"

#include "automation_json.hpp"
#include "automation_server.hpp"

#include <spectra/app.hpp>

#include "render/backend.hpp"
#include "ui/app/window_ui_context.hpp"

#include <sstream>

namespace spectra
{

bool json_has_key(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\"";
    return json.find(search) != std::string::npos;
}

bool check_automation_context(AutomationContextFlag flags,
                              App*                  app,
                              WindowUIContext*      ui_ctx,
                              std::string&          error)
{
    using F = AutomationContextFlag;

    if (has_automation_flag(flags, F::UiContext) && !ui_ctx)
    {
        error = "No UI context";
        return false;
    }

#ifdef SPECTRA_USE_IMGUI
    if (has_automation_flag(flags, F::ImGui) && !ui_ctx)
    {
        error = "No UI context";
        return false;
    }
#else
    if (has_automation_flag(flags, F::ImGui))
    {
        error = "ImGui not available";
        return false;
    }
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    (void)0;
#else
    if (has_automation_flag(flags, F::Windowing))
    {
        error = "No windowing backend";
        return false;
    }
#endif

    if (has_automation_flag(flags, F::Backend) && (!app || !app->backend()))
    {
        error = "No backend available";
        return false;
    }

    if (has_automation_flag(flags, F::FigureMgr))
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ui_ctx || !ui_ctx->fig_mgr)
        {
            error = "No figure manager";
            return false;
        }
#else
        error = "ImGui not available";
        return false;
#endif
    }

    return true;
}

bool validate_automation_params(const std::string&            params_json,
                                const std::vector<ParamSpec>& params,
                                std::string&                  error)
{
    for (const auto& spec : params)
    {
        if (!spec.required || !spec.name)
            continue;

        if (!json_has_key(params_json, spec.name))
        {
            error = std::string("Missing parameter: ") + spec.name;
            return false;
        }

        if (spec.kind == ParamKind::String && json_get_string(params_json, spec.name).empty())
        {
            error = std::string("Missing or empty parameter: ") + spec.name;
            return false;
        }
    }
    return true;
}

AutomationHandlerFn wrap_automation_handler(AutomationHandlerEntry entry)
{
    return [entry = std::move(entry)](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
    {
        std::string err;
        if (!check_automation_context(entry.context, &app, ui_ctx, err))
        {
            req.response_json = json_error(req.id, err);
            return;
        }
        if (!validate_automation_params(req.params_json, entry.params, err))
        {
            req.response_json = json_error(req.id, err);
            return;
        }
        entry.handler(req, app, ui_ctx);
    };
}

namespace
{

const char* param_kind_name(ParamKind kind)
{
    switch (kind)
    {
        case ParamKind::String:
            return "string";
        case ParamKind::Int:
            return "integer";
        case ParamKind::Number:
            return "number";
    }
    return "unknown";
}

const char* context_flag_name(AutomationContextFlag flag)
{
    switch (flag)
    {
        case AutomationContextFlag::UiContext:
            return "ui_context";
        case AutomationContextFlag::ImGui:
            return "imgui";
        case AutomationContextFlag::Windowing:
            return "windowing";
        case AutomationContextFlag::Backend:
            return "backend";
        case AutomationContextFlag::FigureMgr:
            return "figure_mgr";
        default:
            return nullptr;
    }
}

}   // namespace

std::string serialize_handler_catalog(const std::vector<AutomationHandlerEntry>& catalog)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < catalog.size(); ++i)
    {
        if (i > 0)
            oss << ",";
        const auto& e = catalog[i];
        oss << "{\"method\":\"" << json_escape(e.method) << "\",\"description\":\""
            << json_escape(e.description) << "\",\"context\":[";
        bool first_ctx = true;
        for (uint8_t bit = 1; bit <= static_cast<uint8_t>(AutomationContextFlag::FigureMgr);
             bit <<= 1)
        {
            const auto flag = static_cast<AutomationContextFlag>(bit);
            if (!has_automation_flag(e.context, flag))
                continue;
            if (const char* name = context_flag_name(flag))
            {
                if (!first_ctx)
                    oss << ",";
                first_ctx = false;
                oss << "\"" << name << "\"";
            }
        }
        oss << "],\"params\":[";
        for (size_t pi = 0; pi < e.params.size(); ++pi)
        {
            if (pi > 0)
                oss << ",";
            const auto& p = e.params[pi];
            oss << "{\"name\":\"" << json_escape(p.name ? p.name : "") << "\",\"type\":\""
                << param_kind_name(p.kind) << "\",\"required\":" << (p.required ? "true" : "false")
                << "}";
        }
        oss << "]}";
    }
    oss << "]";
    return oss.str();
}

}   // namespace spectra
