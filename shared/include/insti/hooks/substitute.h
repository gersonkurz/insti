#pragma once

// =============================================================================
// insti/hooks/substitute.h - Variable substitution hook
// =============================================================================

#include "hook.h"
#include <insti/core/phase.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

/// Hook to substitute variables in text files.
///
/// Direction depends on execution phase:
/// - PreBackup: Replace known values with ${VARNAME} placeholders
/// - PostRestore: Replace ${VARNAME} placeholders with resolved values
///
/// File pattern supports glob syntax and variable substitution.
class SubstituteHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(SubstituteHook)

public:
    static constexpr std::string_view TYPE_NAME = "substitute";

    /// @param file_pattern File path or glob pattern (may contain variables)
    explicit SubstituteHook(std::string file_pattern)
        : IHook{std::string{TYPE_NAME}}
        , m_file_pattern{std::move(file_pattern)}
    {}

    /// Set the execution phase (called by orchestrator before execute).
    void set_phase(Phase phase) { m_phase = phase; }

    /// @name Accessors
    /// @{
    Phase phase() const { return m_phase; }
    const std::string& file_pattern() const { return m_file_pattern; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    bool substitute_to_placeholders(const std::string& file_path,
                                    const std::unordered_map<std::string, std::string>& variables) const;

    bool substitute_from_placeholders(const std::string& file_path,
                                      const std::unordered_map<std::string, std::string>& variables) const;

    std::vector<std::string> expand_glob(const std::string& resolved_pattern) const;

    const std::string m_file_pattern;
    Phase m_phase{Phase::PreBackup}; ///< Mutable - set by orchestrator before execute
};

} // namespace insti
