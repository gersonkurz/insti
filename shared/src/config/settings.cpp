#include "pch.h"
#include <insti/config/settings.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>

namespace insti
{
namespace config
{

Settings theSettings;

void initialize_logging()
{
    auto& loggingSettings = theSettings.logging;

    // Determine log file path
    auto logFilePath = loggingSettings.logFilePath.get();
    if (logFilePath.empty())
    {
        logFilePath = Settings::default_log_path().string();
    }

    // Ensure directory exists
    std::filesystem::path logPath{logFilePath};
    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);

    // Setup spdlog with file sink only (no console output)
    try
    {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);
        auto logger = std::make_shared<spdlog::logger>("insti", file_sink);

        // Set log level from config
        const auto logLevel = loggingSettings.logLevel.get();
        logger->set_level(spdlog::level::from_str(logLevel));

        // Flush on every log message (important for debugging crashes/hangs)
        logger->flush_on(spdlog::level::trace);

        spdlog::set_default_logger(logger);
        spdlog::info("Logging initialized - file: {}, level: {}", logFilePath, logLevel);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        // Fall back to console only
        spdlog::error("Failed to create file logger: {}", ex.what());
    }
}

} // namespace config
} // namespace insti
