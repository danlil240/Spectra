#pragma once

// figure_export.hpp — Render a backend-owned figure to a PNG / SVG file.
//
// Serves REQ_EXPORT_FIGURE (Python savefig): the daemon's FigureModel state
// is rebuilt into a live spectra::Figure with the window agent's snapshot
// builder, so the file shows what a window would, and drawn by a headless
// App (offscreen Vulkan; no window or display needed).

#include <cstdint>
#include <string>

#include "../ipc/message.hpp"

namespace spectra::daemon
{

// Largest PNG edge accepted from a client, in pixels.
inline constexpr uint32_t MAX_EXPORT_EDGE = 16384;

struct FigureExportResult
{
    uint32_t    code = 0;   // 0 = written; otherwise the RESP_ERR code (400, 500, 503)
    std::string message;

    bool ok() const { return code == 0; }
};

// "png" or "svg" from an explicit format (case-insensitive) or, when that is
// empty, from the path's extension (".svg" → svg, anything else → png).
// Returns "" for an unsupported explicit format.
std::string resolve_export_format(const std::string& format, const std::string& path);

// Check a request without rendering: absolute file path in an existing
// writable directory, supported format, PNG size within MAX_EXPORT_EDGE.
FigureExportResult validate_export_request(const ipc::ReqExportFigurePayload& req);

// Build `figure` and write it to req.path.  The image is written to a temporary
// file next to the destination and renamed over it only once complete, so a
// failed export leaves an existing file untouched.  Blocks until done.
FigureExportResult export_figure_snapshot(const ipc::SnapshotFigureState&    figure,
                                          const ipc::ReqExportFigurePayload& req);

}   // namespace spectra::daemon
