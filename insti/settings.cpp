#include "pch.h"
#include "settings.h"
#include <filesystem>

namespace insti
{

    namespace fs = std::filesystem;

    bool RegistrySettings::load(std::string_view path)
    {
        // Create parent directory if it doesn't exist
        fs::path config_path{ path };
        if (config_path.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(config_path.parent_path(), ec);
        }

        pnq::config::TomlBackend backend{ std::string{path} };
        return Section::load(backend);
    }

    bool RegistrySettings::save(std::string_view path) const
    {
        // Create parent directory if it doesn't exist
        fs::path config_path{ path };
        if (config_path.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(config_path.parent_path(), ec);
        }

        pnq::config::TomlBackend backend{ std::string{path} };
        return Section::save(backend);
    }

    std::string RegistrySettings::default_config_path()
    {
        auto appdata = pnq::path::get_roaming_app_data("insti");
        return (appdata / "registry.toml").string();
    }

} // namespace insti
