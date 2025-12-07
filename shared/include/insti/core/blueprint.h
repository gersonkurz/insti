#pragma once

// =============================================================================
// insti/core/blueprint.h - In-memory blueprint representation
// =============================================================================

#include <string>
#include <string_view>
#include <unordered_map>
#include <pnq/pnq.h>
#include <pnq/ref_counted.h>
#include <insti/core/phase.h>
#include <insti/actions/action.h>
#include <insti/hooks/hook.h>

namespace insti
{

    /// In-memory representation of a blueprint.
    ///
    /// A blueprint defines the resources (files, registry, services, etc.) and hooks
    /// that comprise an application state snapshot. It owns variable resolution and
    /// manages the lifecycle of all contained actions and hooks.
    ///
    /// Reference-counted - use PNQ_ADDREF/PNQ_RELEASE for ownership management.
    /// Factory methods return a pointer with refcount=1 (caller owns initial ref).
    class Blueprint : public pnq::RefCountImpl
    {
        PNQ_DECLARE_NON_COPYABLE(Blueprint)

    public:
        /// @name Built-in Variable Names
        /// @{
        static constexpr std::string_view VAR_PROJECT_NAME = "PROJECT_NAME";
        static constexpr std::string_view VAR_PROJECT_VERSION = "PROJECT_VERSION";
        static constexpr std::string_view VAR_PROJECT_DESCRIPTION = "PROJECT_DESCRIPTION";
        static constexpr std::string_view VAR_INSTALLDIR = "INSTALLDIR";
        /// @}

        /// @name Factory Methods
        /// @{

        /// Load blueprint from file.
        /// @param path Path to blueprint file (XML format)
        /// @return Blueprint pointer on success (caller owns ref), nullptr on failure.
        ///         Errors are logged automatically.
        static Blueprint *load_from_file(std::string_view path);

        /// Load blueprint from string.
        /// @param xml Blueprint content (XML format)
        /// @return Blueprint pointer on success (caller owns ref), nullptr on failure.
        ///         Errors are logged automatically.
        static Blueprint *load_from_string(std::string_view xml);

        /// @}

        /// @name Serialization
        /// @{

        /// Serialize blueprint to XML string.
        /// @return XML representation of the blueprint
        virtual std::string to_xml() const;

        /// @}

        /// @name Project Metadata Accessors
        /// @{

        /// Get project name from resolved variables.
        const std::string &name() const { return get_var(VAR_PROJECT_NAME); }

        /// Get project version from resolved variables.
        const std::string &version() const { return get_var(VAR_PROJECT_VERSION); }

        /// Get project description from resolved variables.
        const std::string &description() const { return get_var(VAR_PROJECT_DESCRIPTION); }

        /// Get installation directory (resolved).
        /// This is the primary installation location where the instance blueprint will be written.
        const std::string &installdir() const { return get_var(VAR_INSTALLDIR); }


        
        /// @}

        /// @name Resource Accessors
        /// @{

        /// Get all actions defined in this blueprint.
        /// @return Reference to action vector (valid while blueprint alive)
        const pnq::RefCountedVector<IAction *> &actions() const { return m_actions; }

        /// Get hooks for a specific phase.
        /// @param phase The execution phase
        /// @return Reference to hook vector for that phase (valid while blueprint alive)
        const pnq::RefCountedVector<IHook *> &hooks(Phase phase) const { return m_hooks[static_cast<size_t>(phase)]; }

        /// @}

        /// @name Variable Resolution
        /// @{

        /// Get raw user-defined variables (before resolution).
        const std::unordered_map<std::string, std::string> &user_variables() const { return m_user_variables; }

        /// Get combined resolved variable map (built-ins + user-defined).
        const std::unordered_map<std::string, std::string> &resolved_variables() const { return m_resolved_variables; }

        /// Resolve all variables in a string.
        /// Supports ${VAR} and %VAR% placeholder syntax.
        /// @param input String containing variable placeholders
        /// @return String with all placeholders replaced by values
        std::string resolve(std::string_view input) const;

        /// Reverse variable resolution - replace values with placeholders.
        /// Used during backup to make content portable (e.g., replace "MYPC" with "${COMPUTERNAME}").
        /// Matches longest values first. Case-insensitive for path-like variables.
        /// @param input String containing literal values
        /// @return String with values replaced by ${VAR} placeholders
        std::string unresolve(std::string_view input) const;

        /// Set a variable override (applies on top of resolved variables).
        /// TODO: Move to ActionContext once CLI is rewritten (Phase 3).
        ///       Overrides should be runtime context, not blueprint mutation.
        /// @param name Variable name
        /// @param value Override value (may contain ${VAR} references)
        void set_override(std::string_view name, std::string_view value);

        /// @}

    protected:
        Blueprint() = default;
        ~Blueprint();

        friend class ProjectBlueprint;
        friend class InstanceBlueprint;

        /// Internal: parse XML content into this blueprint.
        /// @param xml XML string to parse
        /// @return true on success, false on failure (errors logged)
        bool parse_xml(std::string_view xml);

    private:

        /// Populate built-in variables from Windows APIs.
        void populate_builtins();

        /// Resolve user-defined variables (handles dependencies).
        /// @return true on success, false if cycle detected
        bool resolve_user_variables();

        /// Get variable value by name.
        /// @param name Variable name to look up
        /// @return Reference to value, or empty string if not found
        const std::string &get_var(std::string_view name) const;

        pnq::RefCountedVector<IAction *> m_actions;
        pnq::RefCountedVector<IHook *> m_hooks[6]; ///< Hooks per phase (6 phases)

        std::unordered_map<std::string, std::string> m_user_variables;     ///< Raw user-defined variables
        std::unordered_map<std::string, std::string> m_builtin_variables;  ///< Built-in variables from system
        std::unordered_map<std::string, std::string> m_resolved_variables; ///< Combined resolved variables
    };

} // namespace insti
