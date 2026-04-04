// export_csv.cpp — Example Spectra plugin: CSV series data exporter
//
// Build as a shared library:
//   g++ -shared -fPIC -std=c++20 -o export_csv.so export_csv.cpp
//       -I<spectra>/include -I<spectra>/src
//
// Place the resulting .so in ~/.config/spectra/plugins/ or load via View → Plugins.
//
// When loaded, this plugin registers a "CSV Data" export format in
// File → Export As.  The callback writes one header row and one data row per
// series using the JSON description supplied by the host.  No image data is
// used (data-only export).

#include "ui/workspace/plugin_api.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ─── Minimal JSON helpers ─────────────────────────────────────────────────────
//
// We only need to walk the "series" array inside the figure JSON and extract
// a handful of string keys.  A full JSON parser would be overkill for an
// example plugin; instead we use simple substring searches.

static std::string extract_string_field(const std::string& json,
                                        const std::string& key,
                                        size_t             start_pos = 0)
{
    std::string needle = "\"" + key + "\":\"";
    size_t      pos    = json.find(needle, start_pos);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos)
        return {};
    return json.substr(pos, end - pos);
}

// Extract all top-level "label" values from the JSON.  Returns one label
// per series object found anywhere in the JSON.
static std::vector<std::string> extract_series_labels(const std::string& json)
{
    std::vector<std::string> labels;
    size_t                   search_from = 0;
    while (true)
    {
        std::string label = extract_string_field(json, "label", search_from);
        if (label.empty())
            break;
        labels.push_back(label);
        // Advance past the found "label":"..." to avoid re-matching.
        size_t needle_pos = json.find("\"label\":\"", search_from);
        if (needle_pos == std::string::npos)
            break;
        search_from = needle_pos + 9 + label.size() + 1;
    }
    return labels;
}

// ─── CSV export callback ──────────────────────────────────────────────────────

static int csv_export(const spectra::SpectraExportContext* ctx, void* /*user_data*/)
{
    if (!ctx || !ctx->output_path || !ctx->figure_json)
        return 1;

    std::ofstream out(ctx->output_path);
    if (!out.is_open())
        return 1;

    std::string json(ctx->figure_json, ctx->figure_json_len);

    // Write a metadata header as a CSV comment.
    out << "# Spectra CSV Export\n";
    out << "# Output: " << ctx->output_path << "\n";
    out << "# Width: " << ctx->pixel_width << "  Height: " << ctx->pixel_height << "\n";

    // Write column headers: one column per series label.
    auto labels = extract_series_labels(json);
    if (labels.empty())
    {
        // Fallback if no labels found — output raw JSON as a single field.
        out << "figure_json\n";
        out << json << "\n";
    }
    else
    {
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (i > 0)
                out << ',';
            // Escape labels that contain commas.
            if (labels[i].find(',') != std::string::npos)
                out << '"' << labels[i] << '"';
            else
                out << labels[i];
        }
        out << '\n';

        // The host currently passes JSON, not raw numeric data.  A real
        // implementation would decode series values from the JSON.  Here we
        // write a single placeholder row noting the source.
        out << "# (Series data not yet available in JSON schema v1; "
               "upgrade to schema v2 for numeric rows)\n";
    }

    return out.good() ? 0 : 1;
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    int spectra_plugin_init(const spectra::SpectraPluginContext* ctx,
                            spectra::SpectraPluginInfo*          info)
    {
        info->name              = "Export: CSV Data";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Exports plot data as comma-separated values (.csv)";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        // Requires API v1.3+ for export format registry.
        if (ctx->api_version_minor >= 3 && ctx->export_format_registry)
        {
            spectra_register_export_format(ctx->export_format_registry,
                                           "CSV Data",
                                           "csv",
                                           csv_export,
                                           nullptr);
        }

        return 0;
    }

    void spectra_plugin_shutdown() {}
}
