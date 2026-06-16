// automation_handler.hpp — Handler registry for automation server dispatch.
// Each handler group provides a function returning a vector of entries with
// metadata (required context, parameter specs) used by automation_dispatch.

#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace spectra
{

class App;
struct AutomationRequest;
struct WindowUIContext;

using AutomationHandlerFn =
    std::function<void(AutomationRequest& req, App* app, WindowUIContext* ui_ctx)>;

enum class AutomationContextFlag : uint8_t
{
    None      = 0,
    UiContext = 1 << 0,
    ImGui     = 1 << 1,
    Windowing = 1 << 2,
    Backend   = 1 << 3,
    FigureMgr = 1 << 4,
};

constexpr AutomationContextFlag operator|(AutomationContextFlag a, AutomationContextFlag b)
{
    return static_cast<AutomationContextFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr AutomationContextFlag operator&(AutomationContextFlag a, AutomationContextFlag b)
{
    return static_cast<AutomationContextFlag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool has_automation_flag(AutomationContextFlag flags, AutomationContextFlag bit)
{
    return (flags & bit) != AutomationContextFlag::None;
}

enum class ParamKind : uint8_t
{
    String,
    Int,
    Number,
};

struct ParamSpec
{
    const char* name     = nullptr;
    ParamKind   kind     = ParamKind::String;
    bool        required = true;
};

struct AutomationHandlerEntry
{
    std::string            method;
    std::string            description;
    AutomationContextFlag  context = AutomationContextFlag::None;
    std::vector<ParamSpec> params;
    AutomationHandlerFn    handler;
};

inline AutomationHandlerEntry automation_handler(std::string                      method,
                                                 std::string                      description,
                                                 AutomationContextFlag            context,
                                                 std::initializer_list<ParamSpec> params,
                                                 AutomationHandlerFn              handler)
{
    return AutomationHandlerEntry{std::move(method),
                                  std::move(description),
                                  context,
                                  std::vector<ParamSpec>(params),
                                  std::move(handler)};
}

// ── Handler group factories ──────────────────────────────────────────────────

std::vector<AutomationHandlerEntry> make_command_handlers();
std::vector<AutomationHandlerEntry> make_input_handlers();
std::vector<AutomationHandlerEntry> make_figure_handlers();
std::vector<AutomationHandlerEntry> make_capture_handlers();
std::vector<AutomationHandlerEntry> make_window_handlers();
std::vector<AutomationHandlerEntry> make_utility_handlers();
std::vector<AutomationHandlerEntry> make_fuzz_handlers();

}   // namespace spectra
