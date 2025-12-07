#pragma once

// =============================================================================
// settings.h - Application settings for instinctiv
// =============================================================================

#include <pnq/config/section.h>
#include <pnq/config/typed_value.h>

namespace instinctiv
{
namespace config
{

/// Root configuration section containing all application settings.
class RootSettings : public pnq::config::Section
{
public:
    RootSettings()
        : Section{}
    {
    }

    struct LoggingSettings : public pnq::config::Section
    {
        LoggingSettings(Section* pParent)
            : Section{pParent, "Logging"}
        {
        }
        pnq::config::TypedValue<std::string> logLevel{this, "LogLevel", "debug"};
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
        pnq::config::TypedValue<std::string> theme{this, "Theme", "Dark"};
        pnq::config::TypedValue<std::string> lastBlueprint{this, "LastBlueprint", ""}; // Last selected blueprint name
    } application{this};

    struct RegistrySettings : public pnq::config::Section
    {
        RegistrySettings(Section* pParent)
            : Section{pParent, "Registry"}
        {
        }
        // Comma-separated list of registry root paths
        pnq::config::TypedValue<std::string> roots{this, "Roots", "C:\\ProgramData\\insti"};
        pnq::config::TypedValue<std::string> defaultOutputDir{this, "DefaultOutputDir", ""}; // Empty = same as first root
    } registry{this};
};

extern RootSettings theSettings;

} // namespace config
} // namespace instinctiv
