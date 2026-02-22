// app.cpp — Shared App code: constructor, destructor, figure(), run() dispatcher.
// Mode-specific implementations live in app_inproc.cpp and app_multiproc.cpp.

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"

namespace spectra
{

// ─── App ─────────────────────────────────────────────────────────────────────

App::App(const AppConfig& config) : config_(config)
{
    // Initialize logger for debugging
    // Set to Trace for maximum debugging, Debug for normal debugging, Info for production
    auto& logger = spectra::Logger::instance();
    logger.set_level(spectra::LogLevel::Debug);   // Change to Trace to see all frame-by-frame logs

    // Add console sink with timestamps
    logger.add_sink(spectra::sinks::console_sink());

    // Add file sink in temp directory with error handling
    try
    {
        std::string log_path = (std::filesystem::temp_directory_path() / "spectra_app.log").string();
        logger.add_sink(spectra::sinks::file_sink(log_path));
        SPECTRA_LOG_INFO("app", "Log file: " + log_path);
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_WARN("app", "Failed to create log file: " + std::string(e.what()));
    }

    SPECTRA_LOG_INFO("app",
                     "Initializing Spectra application (headless: "
                         + std::string(config_.headless ? "true" : "false") + ")");

    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = (env && env[0] != '\0');
    }
    SPECTRA_LOG_INFO("app", "Runtime mode: " + std::string(multiproc ? "multiproc" : "inproc"));

    // Create Vulkan backend
    backend_ = std::make_unique<VulkanBackend>();
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    // Create renderer
    renderer_ = std::make_unique<Renderer>(*backend_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }

    SPECTRA_LOG_INFO("app", "Spectra application initialized successfully");
}

App::~App()
{
    // Destroy renderer before backend (renderer holds backend reference)
    renderer_.reset();
    if (backend_)
    {
        backend_->shutdown();
    }
}

Figure& App::figure(const FigureConfig& config)
{
    auto id = registry_.register_figure(std::make_unique<Figure>(config));
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
        while (sibling_map_.count(cur))
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
    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = (env && env[0] != '\0');
    }

    if (multiproc)
        run_multiproc();
    else
        run_inproc();
}

}   // namespace spectra
