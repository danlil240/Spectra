// export_csv.cpp — Example Spectra plugin: CSV series data exporter
//
// Build as a shared library:
//   g++ -shared -fPIC -std=c++20 -I<spectra>/include -I<spectra>/src -o export_csv.so
//   export_csv.cpp
//
// Place the resulting .so in ~/.config/spectra/plugins/ or load via View → Plugins.
//
// When loaded, this plugin registers a "CSV Data" export format in
// File → Export As.  The callback converts the figure JSON supplied by the host
// into RFC 4180 CSV with one x,y column pair per series (see
// export_csv_writer.hpp for the JSON keys it reads).  No image data is used
// (data-only export).  Series that carry no "x"/"y" arrays in the JSON produce
// header columns only.

#include "ui/workspace/plugin_api.hpp"

#include <fstream>
#include <string>
#include <string_view>

#include "export_csv_writer.hpp"

// ─── CSV export callback ──────────────────────────────────────────────────────

static int csv_export(const spectra::SpectraExportContext* ctx, void* /*user_data*/)
{
    if (!ctx || !ctx->output_path || !ctx->figure_json)
        return 1;

    std::string csv;
    if (!spectra_examples::csv::figure_json_to_csv(
            std::string_view(ctx->figure_json, ctx->figure_json_len),
            csv))
        return 1;

    std::ofstream out(ctx->output_path, std::ios::binary);
    if (!out.is_open())
        return 1;
    out << csv;
    return out.good() ? 0 : 1;
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    SPECTRA_PLUGIN_API int spectra_plugin_init(const spectra::SpectraPluginContext* ctx,
                                               spectra::SpectraPluginInfo*          info)
    {
        info->name              = "Export: CSV Data";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Exports each series' x/y values as CSV columns (.csv)";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        // Requires API v1.3+ for export format registry.
        if (spectra::spectra_plugin_api_at_least(ctx, 1, 3) && ctx->export_format_registry)
        {
            spectra_register_export_format(ctx->export_format_registry,
                                           "CSV Data",
                                           "csv",
                                           csv_export,
                                           nullptr);
        }

        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown() {}
}
