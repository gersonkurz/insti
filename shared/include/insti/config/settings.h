#pragma once

// =============================================================================
// insti/config/settings.h - Shared application settings for insti and instinctiv
// =============================================================================

#include <pnq/config/section.h>
#include <pnq/config/typed_value.h>
#include <pnq/config/toml_backend.h>
#include <pnq/path.h>
#include <string>
#include <filesystem>

namespace insti
{
namespace config
{

/// Root configuration section containing all application settings.
/// Used by both CLI (insti) and GUI (instinctiv).
class Settings : public pnq::config::Section
{
public:
    Settings()
        : Section{}
    {
    }

    struct LoggingSettings : public pnq::config::Section
    {
        LoggingSettings(Section* pParent)
            : Section{pParent, "Logging"}
        {
        }
        pnq::config::TypedValue<std::string> logLevel{this, "LogLevel", "info"};
        pnq::config::TypedValue<std::string> logFilePath{this, "LogFilePath", ""}; // Empty = use default
    } logging{this};

    struct WindowSettings : public pnq::config::Section
    {
        WindowSettings(Section* pParent)
            : Section{pParent, "Window"}
        {
        }
        pnq::config::TypedValue<int32_t> width{this, "Width", 1280};
        pnq::config::TypedValue<int32_t> height{this, "Height", 720};
        pnq::config::TypedValue<int32_t> positionX{this, "PositionX", 100};
        pnq::config::TypedValue<int32_t> positionY{this, "PositionY", 100};
        pnq::config::TypedValue<bool> maximized{this, "Maximized", false};
    } window{this};

    struct ApplicationSettings : public pnq::config::Section
    {
        ApplicationSettings(Section* pParent)
            : Section{pParent, "Application"}
        {
        }
        pnq::config::TypedValue<int32_t> fontSizeScaled{this, "FontSize", 1600}; // Font size * 100 (16.0f -> 1600)
        pnq::config::TypedValue<std::string> fontName{this, "FontName", "Arial"};
        pnq::config::TypedValue<std::string> theme{this, "Theme", "Tomorrow Night Blue"};
        pnq::config::TypedValue<std::string> lastBlueprint{this, "LastBlueprint", ""};
    } application{this};

    struct RegistrySettings : public pnq::config::Section
    {
        RegistrySettings(Section* pParent)
            : Section{pParent, "Registry"}
        {
        }
        pnq::config::TypedValue<std::string> roots{this, "Roots", "C:\\ProgramData\\insti"};
        pnq::config::TypedValue<std::string> defaultOutputDir{this, "DefaultOutputDir", ""};
    } registry{this};

    /// Get the default config file path: %LOCALAPPDATA%\insti\insti.toml
    static std::filesystem::path default_path()
    {
        return pnq::path::get_known_folder(FOLDERID_LocalAppData) / "insti" / "insti.toml";
    }

    /// Get the default log file path: %LOCALAPPDATA%\insti\insti.log
    static std::filesystem::path default_log_path()
    {
        return pnq::path::get_known_folder(FOLDERID_LocalAppData) / "insti" / "insti.log";
    }

    /// Load from default path
    bool load()
    {
        pnq::config::TomlBackend backend{default_path().string()};
        return Section::load(backend);
    }

    /// Save to default path
    bool save()
    {
        pnq::config::TomlBackend backend{default_path().string()};
        return Section::save(backend);
    }
};

/// Global settings instance
extern Settings theSettings;

/// Initialize logging based on settings.
/// Call after loading settings.
void initialize_logging();

} // namespace config
} // namespace insti
