#pragma once

#include <insti/actions/action.h>
#include <string>
#include <vector>
#include <optional>
#include <pnq/pnq.h>

namespace insti
{

    /// Windows service configuration (serialized to TOML in the snapshot).
    struct ServiceConfig
    {
        std::string name;          ///< Service name (internal)
        std::string display_name;  ///< Display name
        std::string description;   ///< Description
        std::string binary_path;   ///< Path to executable
        uint32_t start_type = 0;   ///< SERVICE_AUTO_START, etc.
        uint32_t service_type = 0; ///< SERVICE_WIN32_OWN_PROCESS, etc.
        std::string account;       ///< Account name (empty = LocalSystem)
        std::vector<std::string> dependencies;
        bool was_running = false; ///< Was the service running at backup time?

        std::string to_toml() const;
        static std::optional<ServiceConfig> from_toml(std::string_view toml);
    };

    /// Captures and restores a Windows service configuration.
    ///
    /// On backup, reads service configuration from SCM and records running state.
    /// On restore, creates/updates the service and optionally starts it.
    /// On clean, stops and deletes the service.
    class ServiceAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(ServiceAction)

    public:
        static constexpr std::string_view TYPE_NAME = "service";

        /// @param name Windows service name
        /// @param archive_path Path within the snapshot for the TOML config
        /// @param description Optional user-facing description
        ServiceAction(std::string name, std::string archive_path, std::string description = {});

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &name() const { return m_name; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

        /// @name Direct SCM operations (for testing and standalone use)
        /// @{
        std::optional<ServiceConfig> read_config() const;
        bool write_config(const ServiceConfig &config) const;
        bool delete_service() const;
        bool start_service() const;
        bool stop_service() const;
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        const std::string m_name;
        const std::string m_archive_path;
    };

} // namespace insti
