#pragma once

// =============================================================================
// insti/hooks/run_process.h - Run external process hook
// =============================================================================

#include "hook.h"
#include <string>
#include <vector>
#include <pnq/pnq.h>

namespace insti
{

/// Hook to run an external process.
///
/// Can optionally wait for completion and check exit code.
/// Paths and arguments support variable substitution.
class RunProcessHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(RunProcessHook)

public:
    static constexpr std::string_view TYPE_NAME = "run";

    /// @param path Executable path (may contain variables)
    /// @param args Command line arguments (may contain variables)
    /// @param wait Wait for process to complete
    /// @param ignore_exit_code Don't fail on non-zero exit code
    RunProcessHook(std::string path, std::vector<std::string> args = {},
                   bool wait = true, bool ignore_exit_code = false)
        : IHook{std::string{TYPE_NAME}}
        , m_path{std::move(path)}
        , m_args{std::move(args)}
        , m_wait{wait}
        , m_ignore_exit_code{ignore_exit_code}
    {}

    /// @name Accessors
    /// @{
    const std::string& path() const { return m_path; }
    const std::vector<std::string>& args() const { return m_args; }
    bool wait() const { return m_wait; }
    bool ignore_exit_code() const { return m_ignore_exit_code; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    const std::string m_path;
    const std::vector<std::string> m_args;
    const bool m_wait;
    const bool m_ignore_exit_code;
};

} // namespace insti
