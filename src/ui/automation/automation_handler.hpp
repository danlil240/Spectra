// automation_handler.hpp — Handler registry for automation server dispatch.
// Each handler group provides a function returning a vector of entries.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace spectra
{

class App;
struct AutomationRequest;
struct WindowUIContext;

// Signature for a single automation handler.
using AutomationHandlerFn =
    std::function<void(AutomationRequest& req, App& app, WindowUIContext* ui_ctx)>;

// Entry in the handler table: method name -> handler function.
struct AutomationHandlerEntry
{
    std::string         method;
    AutomationHandlerFn handler;
};

// ── Handler group factories ──────────────────────────────────────────────────
// Each returns a list of entries for its domain.

std::vector<AutomationHandlerEntry> make_command_handlers();
std::vector<AutomationHandlerEntry> make_input_handlers();
std::vector<AutomationHandlerEntry> make_figure_handlers();
std::vector<AutomationHandlerEntry> make_capture_handlers();
std::vector<AutomationHandlerEntry> make_window_handlers();
std::vector<AutomationHandlerEntry> make_utility_handlers();

}   // namespace spectra
