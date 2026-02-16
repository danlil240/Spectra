#include <chrono>
#include <spectra/app.hpp>
#include <spectra/logger.hpp>
#include <thread>

using namespace spectra;

int main()
{
    // Initialize logger with console output
    Logger::instance().set_level(LogLevel::Debug);
    Logger::instance().add_sink(sinks::console_sink());

    // Also log to file
    Logger::instance().add_sink(sinks::file_sink("plotix_example.log"));

    PLOTIX_LOG_INFO("example", "Logger example starting up");

    // Test different log levels
    PLOTIX_LOG_TRACE("example", "This is a trace message");
    PLOTIX_LOG_DEBUG("example", "Debug information: value = {}", 42);
    PLOTIX_LOG_INFO("example", "Application initialized successfully");
    PLOTIX_LOG_WARN("example", "This is a warning message");
    PLOTIX_LOG_ERROR("example", "This is an error message");
    PLOTIX_LOG_CRITICAL("example", "Critical system failure!");

    // Test logging with location information
    PLOTIX_LOG_INFO_HERE("example", "Logging with source location");

    // Test thread safety
    auto worker = [](int id)
    {
        for (int i = 0; i < 5; ++i)
        {
            PLOTIX_LOG_DEBUG("worker", "Worker {} iteration {}", id, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);

    t1.join();
    t2.join();

    PLOTIX_LOG_INFO("example", "Logger example completed");

    return 0;
}
