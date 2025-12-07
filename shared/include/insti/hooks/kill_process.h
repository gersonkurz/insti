#pragma once

// =============================================================================
// insti/hooks/kill_process.h - Kill process hook
// =============================================================================

#include "hook.h"
#include <string>
#include <pnq/pnq.h>

namespace insti
{

/// Hook to kill a process by name.
///
/// Attempts graceful shutdown first (WM_CLOSE), then forcefully terminates
/// if the timeout expires.
class KillProcessHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(KillProcessHook)

public:
    static constexpr std::string_view TYPE_NAME = "kill";

    /// @param process_name Process name to kill (e.g., "notepad.exe")
    /// @param timeout_ms Timeout in milliseconds for graceful shutdown before force kill
    explicit KillProcessHook(std::string process_name, uint32_t timeout_ms = 5000)
        : IHook{std::string{TYPE_NAME}}
        , m_process_name{std::move(process_name)}
        , m_timeout_ms{timeout_ms}
    {}

    /// @name Accessors
    /// @{
    const std::string& process_name() const { return m_process_name; }
    uint32_t timeout_ms() const { return m_timeout_ms; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    const std::string m_process_name;
    const uint32_t m_timeout_ms;
};

} // namespace insti
