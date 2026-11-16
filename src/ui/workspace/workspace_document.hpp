#pragma once

// workspace_document.hpp — the file File > Save writes: a whole window
// (every figure tab with its data, overlays and the window state) in one
// .spectra file.
//
// Format (little endian):
//   Magic "SPWS"(4) + Version u32
//   JSON length u32 + Workspace::serialize_json() of the window state
//   Figure count u32, then per figure: length u32 + FigureSerializer blob
//
// Other .spectra files are a single figure (FigureSerializer, "SPCF") or a
// JSON workspace/layout; sniff_spectra_file() tells them apart.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

struct WorkspaceDocumentContents
{
    std::string              workspace_json;   // Workspace::serialize_json()
    std::vector<std::string> figure_blobs;     // FigureSerializer::save() bytes, tab order
};

enum class SpectraFileKind
{
    Missing,             // cannot be read
    WorkspaceDocument,   // "SPWS": encode_workspace_document()
    Figure,              // "SPCF": FigureSerializer
    Json,                // JSON workspace or layout (Workspace::save, File > Save Layout)
    Unknown,
};

class WorkspaceDocument
{
   public:
    static constexpr uint32_t VERSION = 1;

    static std::string encode(const WorkspaceDocumentContents& contents);
    // False for anything that is not a complete, supported document.
    static bool decode(std::string_view bytes, WorkspaceDocumentContents& out);

    // Atomic write (temp file + rename).  On failure @p error says why.
    static bool write_file(const std::string& path,
                           std::string_view   bytes,
                           std::string*       error = nullptr);
    static bool read_file(const std::string& path, std::string& bytes);

    static bool            has_magic(std::string_view head);
    static SpectraFileKind sniff(std::string_view head);
    static SpectraFileKind sniff_file(const std::string& path);
};

}   // namespace spectra
