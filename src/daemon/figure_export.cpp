#include "figure_export.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "agent/snapshot_figure_builder.hpp"

#ifdef _WIN32
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace spectra::daemon
{

namespace
{

namespace fs = std::filesystem;

std::string lower(std::string s)
{
    std::transform(s.begin(),
                   s.end(),
                   s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

FigureExportResult fail(uint32_t code, std::string message)
{
    return FigureExportResult{code, std::move(message)};
}

bool dir_is_writable(const fs::path& dir)
{
#ifdef _WIN32
    return ::_access(dir.string().c_str(), 2) == 0;
#else
    return ::access(dir.c_str(), W_OK) == 0;
#endif
}

// Unique temporary path next to `dest` that keeps the format's extension.
fs::path temp_path_for(const fs::path& dest, const std::string& format)
{
    static std::atomic<uint64_t> counter{0};
    const auto                   stamp =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string name = "." + dest.filename().string() + ".spectra-" + std::to_string(stamp) + "-"
                       + std::to_string(counter.fetch_add(1)) + "." + format;
    return dest.parent_path() / name;
}

}   // namespace

std::string resolve_export_format(const std::string& format, const std::string& path)
{
    if (!format.empty())
    {
        const std::string f = lower(format);
        return (f == "png" || f == "svg") ? f : std::string{};
    }
    return lower(fs::path(path).extension().string()) == ".svg" ? "svg" : "png";
}

FigureExportResult validate_export_request(const ipc::ReqExportFigurePayload& req)
{
    if (req.path.empty())
        return fail(400, "export: empty path");
    const fs::path p(req.path);
    // The backend's working directory is not the client's: relative paths
    // would land somewhere unexpected, so the client resolves them first.
    if (!p.is_absolute())
        return fail(400, "export: path must be absolute: " + req.path);
    if (resolve_export_format(req.format, req.path).empty())
        return fail(400, "export: unsupported format '" + req.format + "' (use png or svg)");
    if (req.width > MAX_EXPORT_EDGE || req.height > MAX_EXPORT_EDGE)
        return fail(400, "export: width/height must be at most " + std::to_string(MAX_EXPORT_EDGE));

    std::error_code ec;
    if (fs::is_directory(p, ec))
        return fail(400, "export: '" + req.path + "' is a directory");
    const fs::path dir = p.parent_path();
    ec.clear();
    if (!fs::is_directory(dir, ec))
        return fail(400, "export: directory '" + dir.string() + "' does not exist");
    if (!dir_is_writable(dir))
        return fail(400, "export: directory '" + dir.string() + "' is not writable");
    return {};
}

FigureExportResult export_figure_snapshot(const ipc::SnapshotFigureState&    figure,
                                          const ipc::ReqExportFigurePayload& req)
{
    FigureExportResult valid = validate_export_request(req);
    if (!valid.ok())
        return valid;
    const std::string format = resolve_export_format(req.format, req.path);
    const fs::path    dest(req.path);
    const fs::path    tmp = temp_path_for(dest, format);

    {
        // Headless: renders offscreen even when SPECTRA_SOCKET is set.  The
        // user's saved theme and palette apply so the file matches their
        // windows; ExportOptions::theme overrides the theme for this file.
        AppConfig cfg;
        cfg.headless          = true;
        cfg.use_user_settings = true;
        App app(cfg);
        if (!app.ok())
            return fail(503, "export: no render backend available (Vulkan failed to initialize)");

        std::unique_ptr<Figure> fig = agent::build_figure_from_snapshot(figure);
        // A file is one frame: never let a live-streaming figure keep the
        // headless runtime spinning.
        fig->anim_ = FigureAnimState{};
        ExportOptions opts;
        opts.width  = req.width;
        opts.height = req.height;
        opts.theme  = req.theme;
        const bool queued =
            format == "svg" ? fig->save_svg(tmp.string(), opts) : fig->save_png(tmp.string(), opts);
        if (!queued)
            return fail(400, "export: cannot write to '" + req.path + "'");
        app.figure_registry().register_figure(std::move(fig));
        app.run();
    }

    std::error_code ec;
    if (!fs::exists(tmp, ec) || fs::file_size(tmp, ec) == 0 || ec)
    {
        fs::remove(tmp, ec);
        return fail(500, "export: rendering or writing '" + req.path + "' failed");
    }
#ifdef _WIN32
    fs::remove(dest, ec);   // rename() does not replace an existing file on Windows
#endif
    ec.clear();
    fs::rename(tmp, dest, ec);
    if (ec)
    {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail(500, "export: cannot move the image to '" + req.path + "': " + ec.message());
    }
    SPECTRA_LOG_INFO("daemon", "Exported figure {} to {}", figure.figure_id, req.path);
    return {};
}

}   // namespace spectra::daemon
