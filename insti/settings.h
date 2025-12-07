#pragma once

#include <insti/registry/snapshot_registry.h>
#include <pnq/config/section.h>
#include <pnq/config/typed_value.h>
#include <pnq/config/typed_vector_value.h>
#include <pnq/config/toml_backend.h>
#include <string>
#include <string_view>

namespace insti
{
    /// Registry configuration - manages snapshot roots and naming patterns.
    /// @note Inherits from pnq::config::Section, cannot be final.
    struct RegistrySettings : public pnq::config::Section
    {
        RegistrySettings()
            : Section{}
        {
        }

        pnq::config::TypedValue<std::string> path{ this, "Path", "" };  ///< Directory path

        /// Load settings from file. Creates default config if file doesn't exist.
        bool load(std::string_view path);

        /// Save settings to file.
        bool save(std::string_view path) const;

        /// Get the default config file path (%APPDATA%\insti\registry.toml)
        static std::string default_config_path();
    };

} // namespace insti

