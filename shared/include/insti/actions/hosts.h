#pragma once

#include <insti/actions/action.h>
#include <string>
#include <optional>
#include <pnq/pnq.h>

namespace insti
{

    /// Hosts file entry (serialized to TOML in the snapshot).
    struct HostsEntry
    {
        std::string ip;       ///< IP address (e.g., "127.0.0.1")
        std::string hostname; ///< Hostname (e.g., "myapp.local")
        std::string comment;  ///< Optional comment

        std::string to_toml() const;
        static std::optional<HostsEntry> from_toml(std::string_view toml);
    };

    /// Manages a single entry in the Windows hosts file.
    ///
    /// On backup, reads the IP mapping for the hostname (if present).
    /// On restore, adds or updates the hosts file entry.
    /// On clean, removes the entry from the hosts file.
    ///
    /// Creates a backup of the hosts file before any modification.
    class HostsAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(HostsAction)

    public:
        static constexpr std::string_view TYPE_NAME = "hosts";

        /// @param hostname Hostname to manage (e.g., "myapp.local")
        /// @param archive_path Path within the snapshot for the TOML entry
        /// @param description Optional user-facing description
        HostsAction(std::string hostname, std::string archive_path, std::string description = {});

        /// Get the system hosts file path.
        static std::string hosts_file_path();

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &hostname() const { return m_hostname; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

        /// @name Direct hosts file operations (for testing and standalone use)
        /// @{
        std::optional<HostsEntry> read_entry() const;
        bool write_entry(const HostsEntry &entry) const;
        bool delete_entry() const;
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        const std::string m_hostname;
        const std::string m_archive_path;
    };

} // namespace insti
