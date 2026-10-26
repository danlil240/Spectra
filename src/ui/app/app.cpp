// app.cpp — Shared App code: figure(), run() dispatcher, window groups.
// Constructor and destructor are in app_step.cpp (pimpl pattern — AppRuntime
// must be a complete type for std::unique_ptr<AppRuntime> ctor/dtor).
// Mode-specific implementations live in app_inproc.cpp and app_multiproc.cpp.

#include <csignal>
#include <cstdlib>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

namespace spectra
{

Figure& App::figure(const FigureConfig& config)
{
    auto id = registry_.register_figure(std::make_unique<Figure>(config));

    // If runtime is active, notify FigureManager so the new figure
    // appears as a tab and becomes the active figure.
    auto* ui = ui_context();
    if (ui && ui->fig_mgr)
    {
        ui->fig_mgr->add_figure(id, FigureState{});
    }

    return *registry_.get(id);
}

Figure& App::figure(Figure& sibling)
{
    FigureConfig cfg;
    cfg.width   = sibling.width();
    cfg.height  = sibling.height();
    auto new_id = registry_.register_figure(std::make_unique<Figure>(cfg));

    // Record that the new figure should be a tab in the sibling's window
    FigureId sibling_id = registry_.find_id(&sibling);
    if (sibling_id != INVALID_FIGURE_ID)
        sibling_map_[new_id] = sibling_id;

    // If runtime is active, notify FigureManager so the new figure
    // appears as a tab and becomes the active figure.
    auto* ui = ui_context();
    if (ui && ui->fig_mgr)
    {
        ui->fig_mgr->add_figure(new_id, FigureState{});
    }

    return *registry_.get(new_id);
}

std::vector<std::vector<FigureId>> App::compute_window_groups() const
{
    auto all_ids = registry_.all_ids();
    // Build a union-find style grouping: for each figure, find its root
    // (the figure that has no sibling entry, i.e. the first figure in the window).
    std::unordered_map<FigureId, FigureId> root_map;
    for (auto id : all_ids)
    {
        FigureId cur = id;
        while (sibling_map_.contains(cur))
            cur = sibling_map_.at(cur);
        root_map[id] = cur;
    }

    // Group by root, preserving insertion order
    std::vector<std::vector<FigureId>>   groups;
    std::unordered_map<FigureId, size_t> root_to_group;
    for (auto id : all_ids)
    {
        FigureId root = root_map[id];
        auto     it   = root_to_group.find(root);
        if (it == root_to_group.end())
        {
            root_to_group[root] = groups.size();
            groups.push_back({id});
        }
        else
        {
            groups[it->second].push_back(id);
        }
    }
    return groups;
}

void App::run()
{
    if (!initialized_)
    {
        size_t pending_exports = 0;
        for (auto id : registry_.all_ids())
        {
            if (const Figure* f = registry_.get(id))
            {
                const auto& req = f->export_req_;
                if (!req.png_path.empty() || !req.svg_path.empty() || !req.video_path.empty())
                    ++pending_exports;
            }
        }
        SPECTRA_LOG_ERROR("app",
                          "App::run(): Spectra failed to initialize its render backend; no window "
                          "will be shown and {} figure export request(s) will not be written",
                          pending_exports);
        return;
    }

    // Optionally install signal handlers so Ctrl+C (SIGINT) and SIGTERM
    // trigger a graceful shutdown through shutdown_runtime() instead of
    // killing the process mid-frame (which would leak Vulkan resources).
    // Opt-in via AppConfig::install_signal_handlers: a library must not
    // replace its host process's signal dispositions behind its back.
    if (config_.install_signal_handlers)
    {
        std::signal(SIGINT, [](int) { App::request_exit(); });
        std::signal(SIGTERM, [](int) { App::request_exit(); });
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }

    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = ((env != nullptr) && env[0] != '\0');
    }

    if (multiproc)
        run_multiproc();
    else
        run_inproc();
}

}   // namespace spectra
