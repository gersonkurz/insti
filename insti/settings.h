#pragma once

#include <pnq/config/section.h>
#include <pnq/config/typed_value.h>
#include <pnq/config/toml_backend.h>
#include <pnq/path.h>
#include <string>

namespace insti
{
namespace config
{

/// Settings structure compatible with instinctiv's insti.toml
/// Only includes the sections needed by the CLI
class Settings : public pnq::config::Section
{
public:
    Settings()
        : Section{}
    {
    }

    struct RegistrySettings : public pnq::config::Section
    {
        RegistrySettings(Section* pParent)
            : Section{pParent, "Registry"}
        {
        }
        pnq::config::TypedValue<std::string> roots{this, "Roots", "C:\\ProgramData\\insti"};
        pnq::config::TypedValue<std::string> defaultOutputDir{this, "DefaultOutputDir", ""};
    } registry{this};

    /// Load from %LOCALAPPDATA%\insti\insti.toml
    bool load()
    {
        auto config_path = pnq::path::get_known_folder(FOLDERID_LocalAppData) / "insti" / "insti.toml";
        pnq::config::TomlBackend backend{config_path.string()};
        return Section::load(backend);
    }
};

/// Global settings instance
extern Settings theSettings;

} // namespace config
} // namespace insti

