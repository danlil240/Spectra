// automation_security.hpp — Opt-in policy, auth tokens, and path confinement for the
// automation Unix socket and the MCP HTTP server.
//
// The automation endpoints can inject input, run arbitrary UI commands and write files,
// so they are OFF by default and must be enabled explicitly (AppConfig::enable_automation,
// SPECTRA_AUTOMATION=1, or an explicit SPECTRA_AUTO_SOCKET path).  See SECURITY.md.

#pragma once

#include <string>
#include <string_view>

namespace spectra::automation_security
{

// True when the environment variable is set to 1/true/yes/on (case-insensitive).
bool env_flag_enabled(const char* name);

// Decide whether the automation server (and MCP HTTP server) should start.
// Opt-in only: config flag, SPECTRA_AUTOMATION=1, or a non-empty SPECTRA_AUTO_SOCKET.
bool automation_requested(bool config_flag);

// Per-user runtime directory: $XDG_RUNTIME_DIR when it is an existing directory,
// otherwise $TMPDIR, otherwise /tmp.  Never has a trailing slash.
std::string runtime_dir();

// 256-bit random token, hex encoded (64 chars).  Uses getrandom()/std::random_device.
std::string generate_token();

// Length-independent-time comparison for secrets.
bool constant_time_equals(std::string_view a, std::string_view b);

// Atomically (re)create `path` with mode 0600 (O_EXCL | O_NOFOLLOW) and write `contents`.
bool write_private_file(const std::string& path, const std::string& contents);

// Directory that capture tools may write into:
//   $SPECTRA_CAPTURE_DIR, else <runtime_dir>/spectra-captures[-<uid> when not XDG].
std::string capture_dir();

// Create `dir` (mode 0700) if missing and verify it is a real directory (not a symlink)
// owned by the current user.  Returns false and fills `error` otherwise.
bool ensure_private_dir(const std::string& dir, std::string& error);

// Lexically confine a caller-supplied output path to `base_dir`.
//   - empty `requested` → `default_name`
//   - relative paths are resolved inside base_dir
//   - absolute paths must already lie inside base_dir
//   - any ".." component, NUL byte, or empty file name is rejected
// On success `out` holds the absolute confined path.
bool resolve_confined_path(const std::string& requested,
                           const std::string& default_name,
                           const std::string& base_dir,
                           std::string&       out,
                           std::string&       error);

// resolve_confined_path() against capture_dir(), creating the directory (and any
// requested sub-directories) with mode 0700.
bool resolve_capture_path(const std::string& requested,
                          const std::string& default_name,
                          std::string&       out,
                          std::string&       error);

}   // namespace spectra::automation_security
