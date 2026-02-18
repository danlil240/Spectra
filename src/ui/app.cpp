// app.cpp — Shared App code: constructor, destructor, figure(), run() dispatcher.
// Mode-specific implementations live in app_inproc.cpp and app_multiproc.cpp.

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"

#include <filesystem>
#include <memory>

namespace spectra
{

// ─── App ─────────────────────────────────────────────────────────────────────

App::App(const AppConfig& config) : config_(config)
{
    // Initialize logger for debugging
    // Set to Trace for maximum debugging, Debug for normal debugging, Info for production
    auto& logger = spectra::Logger::instance();
    logger.set_level(spectra::LogLevel::Debug);  // Change to Trace to see all frame-by-frame logs

    // Add console sink with timestamps
    logger.add_sink(spectra::sinks::console_sink());

    // Add file sink in temp directory with error handling
    try
    {
        std::string log_path = std::filesystem::temp_directory_path() / "spectra_app.log";
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

void App::run()
{
#ifdef SPECTRA_MULTIPROC
    run_multiproc();
#else
    run_inproc();
#endif
}

}  // namespace spectra
