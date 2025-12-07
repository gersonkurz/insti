#pragma once

#include <insti/actions/action.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

    /// Captures and restores a Windows registry key tree.
    ///
    /// On backup, exports the key and all subkeys/values to a .reg file in the snapshot.
    /// On restore, imports the .reg file back into the registry.
    /// On clean, deletes the entire key tree.
    class RegistryAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(RegistryAction)

    public:
        static constexpr std::string_view TYPE_NAME = "registry";

        /// @param key Registry key path (e.g., "HKLM\\SOFTWARE\\MyApp")
        /// @param archive_path Path within the snapshot for the .reg file
        /// @param description Optional user-facing description (defaults to "Registry: {key}")
        RegistryAction(std::string key, std::string archive_path, std::string description = {})
            : IAction{std::string{TYPE_NAME}, description.empty() ? "Registry: " + key : std::move(description)}
            , m_key{std::move(key)}, m_archive_path{std::move(archive_path)}
        {
        }

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &key() const { return m_key; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        const std::string m_key;
        const std::string m_archive_path;
    };

} // namespace insti
