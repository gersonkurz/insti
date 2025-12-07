#pragma once

#include <insti/actions/action.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

    /// Scope for environment variables.
    enum class EnvironmentScope
    {
        User,  ///< HKCU\Environment
        System ///< HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
    };

    /// Captures and restores a single environment variable.
    ///
    /// On backup, reads the variable from the registry and stores its value.
    /// On restore, writes the value back and broadcasts WM_SETTINGCHANGE.
    /// On clean, deletes the variable.
    class EnvironmentAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(EnvironmentAction)

    public:
        static constexpr std::string_view TYPE_NAME = "environment";

        /// @param name Environment variable name
        /// @param scope User or System scope
        /// @param archive_path Path within the snapshot for the value
        /// @param description Optional user-facing description
        EnvironmentAction(std::string name, EnvironmentScope scope, std::string archive_path, std::string description = {});

        /// Get the registry key path for a given scope.
        static const char *registry_key(EnvironmentScope scope)
        {
            return scope == EnvironmentScope::User
                       ? "HKCU\\Environment"
                       : "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
        }

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &name() const { return m_name; }
        EnvironmentScope scope() const { return m_scope; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

        /// @name Direct registry operations (for testing and standalone use)
        /// @{
        std::string read_value() const;
        bool write_value(const std::string &value) const;
        bool delete_value() const;
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        const std::string m_name;
        const EnvironmentScope m_scope;
        const std::string m_archive_path;
    };

} // namespace insti
