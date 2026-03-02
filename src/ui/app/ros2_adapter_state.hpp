#pragma once

// ros2_adapter_state.hpp — Shared state for the "Tools → ROS2 Adapter" feature.
//
// Provides a process-wide pending-error flag that can be set from the command
// callback (register_commands.cpp) and polled + cleared from the ImGui draw loop
// (imgui_integration.cpp) to show an error modal dialog.
//
// Gated: the flag is always available as a compile-time no-op when
// SPECTRA_USE_ROS2 is OFF so client code can include this header unconditionally.

#include <string>

namespace spectra
{

#ifdef SPECTRA_USE_ROS2

// Returns a reference to the pending error message.
// Non-empty string → show error modal on next ImGui frame, then clear.
// Empty string      → no error pending.
inline std::string& ros2_adapter_pending_error()
{
    static std::string s_pending_error;
    return s_pending_error;
}

// Set a pending error message (called from the command callback on failure).
inline void ros2_adapter_set_error(const std::string& msg)
{
    ros2_adapter_pending_error() = msg;
}

// True when an error is waiting to be displayed.
inline bool ros2_adapter_has_error()
{
    return !ros2_adapter_pending_error().empty();
}

// Clear the pending error (called after the modal has been shown).
inline void ros2_adapter_clear_error()
{
    ros2_adapter_pending_error().clear();
}

#else   // SPECTRA_USE_ROS2 not defined — no-op stubs

inline void        ros2_adapter_set_error(const std::string&) {}
inline bool        ros2_adapter_has_error() { return false; }
inline void        ros2_adapter_clear_error() {}

#endif   // SPECTRA_USE_ROS2

}   // namespace spectra
