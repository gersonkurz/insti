#pragma once

// =============================================================================
// insti/hooks/sql.h - SQL execution hook
// =============================================================================

#include "hook.h"
#include <insti/core/phase.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

/// Hook to execute SQL query on a SQLite database.
///
/// Typically used in PostRestore to patch database values with resolved variables.
/// Both file path and query support variable substitution.
class SqlHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(SqlHook)

public:
    static constexpr std::string_view TYPE_NAME = "sql";

    /// @param file_path Path to SQLite database file (may contain variables)
    /// @param query SQL query to execute (may contain variables)
    explicit SqlHook(std::string file_path, std::string query)
        : IHook{std::string{TYPE_NAME}}
        , m_file_path{std::move(file_path)}
        , m_query{std::move(query)}
    {}

    /// Set the execution phase (called by orchestrator before execute).
    void set_phase(Phase phase) { m_phase = phase; }

    /// @name Accessors
    /// @{
    Phase phase() const { return m_phase; }
    const std::string& file_path() const { return m_file_path; }
    const std::string& query() const { return m_query; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    const std::string m_file_path;
    const std::string m_query;
    Phase m_phase{Phase::PostRestore}; ///< Mutable - set by orchestrator before execute
};

} // namespace insti
