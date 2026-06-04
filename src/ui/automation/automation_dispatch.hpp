// automation_dispatch.hpp — Pre-flight validation and handler wrapping for automation.

#pragma once

#include "automation_handler.hpp"

#include <string>
#include <vector>

namespace spectra
{

class App;
struct WindowUIContext;

bool json_has_key(const std::string& json, const std::string& key);

bool check_automation_context(AutomationContextFlag flags,
                              App*                  app,
                              WindowUIContext*      ui_ctx,
                              std::string&          error);

bool validate_automation_params(const std::string&            params_json,
                                const std::vector<ParamSpec>& params,
                                std::string&                  error);

AutomationHandlerFn wrap_automation_handler(AutomationHandlerEntry entry);

std::string serialize_handler_catalog(const std::vector<AutomationHandlerEntry>& catalog);

}   // namespace spectra
