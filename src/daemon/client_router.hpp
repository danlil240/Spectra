#pragma once

#include <cstdint>
#include <string>

#include "../ipc/message.hpp"

namespace spectra::daemon
{

// Client type classification based on HELLO.client_type field.
enum class ClientType : uint8_t
{
    UNKNOWN = 0,
    AGENT   = 1,   // spectra-window render agent
    PYTHON  = 2,   // Python client (import spectra)
    APP     = 3,   // spectra-app (legacy inproc source client)
};

// Classify a client based on its HELLO payload.
inline ClientType classify_client(const ipc::HelloPayload& hello)
{
    if (hello.client_type == "python")
        return ClientType::PYTHON;
    if (hello.client_type == "agent")
        return ClientType::AGENT;
    // Legacy: detect spectra-app by agent_build string
    if (hello.agent_build.find("spectra-app") != std::string::npos)
        return ClientType::APP;
    // Default: treat as agent (backward compatible)
    return ClientType::AGENT;
}

// Returns true if the message type is a Python-originated request (0x0500-0x053F).
inline bool is_python_request(ipc::MessageType type)
{
    auto t = static_cast<uint16_t>(type);
    return t >= 0x0500 && t <= 0x053F;
}

// Returns true if the message type is a Python response/event (0x0540-0x05FF).
inline bool is_python_response(ipc::MessageType type)
{
    auto t = static_cast<uint16_t>(type);
    return t >= 0x0540 && t <= 0x05FF;
}

}   // namespace spectra::daemon
