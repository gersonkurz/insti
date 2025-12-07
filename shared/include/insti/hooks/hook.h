#pragma once

// =============================================================================
// insti/hooks/hook.h - Base class for execution hooks
// =============================================================================

#include <string>
#include <string_view>
#include <unordered_map>
#include <pnq/ref_counted.h>

namespace insti
{

/// Abstract base class for execution hooks.
///
/// Hooks are executed at specific phases during backup/restore/clean operations.
/// They perform side effects like killing processes, running scripts, or
/// substituting variables in files.
///
/// Reference-counted - use PNQ_ADDREF/PNQ_RELEASE for ownership management.
class IHook : public pnq::RefCountImpl
{
public:
    /// Get the hook type name (e.g., "kill", "run", "substitute").
    const std::string& type_name() const { return m_type_name; }

    /// Execute the hook.
    /// @param variables Resolved variables for substitution
    /// @return true on success
    virtual bool execute(const std::unordered_map<std::string, std::string>& variables) const = 0;

protected:
    /// Construct with type name.
    /// @param type_name Hook type identifier
    explicit IHook(std::string type_name)
        : m_type_name{std::move(type_name)}
    {}

    ~IHook() = default;

private:
    const std::string m_type_name;
};

} // namespace insti
