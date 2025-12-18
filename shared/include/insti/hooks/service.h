#pragma once

// =============================================================================
// insti/hooks/service.h - Service start/stop hooks
// =============================================================================

#include "hook.h"
#include <string>
#include <pnq/pnq.h>

namespace insti
{

/// Hook to start a Windows service.
///
/// Optionally waits for the service to reach running state.
class StartServiceHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(StartServiceHook)

public:
    static constexpr std::string_view TYPE_NAME = "start-service";

    /// @param service_name Service name to start
    /// @param wait If true, wait for service to reach running state
    explicit StartServiceHook(const std::string& service_name, bool wait = true)
        : IHook{std::string{TYPE_NAME}}
        , m_service_name{service_name}
        , m_wait{wait}
    {}

    /// @name Accessors
    /// @{
    const std::string& service_name() const { return m_service_name; }
    bool wait() const { return m_wait; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    const std::string m_service_name;
    const bool m_wait;
};

/// Hook to stop a Windows service.
///
/// Optionally waits for the service to reach stopped state.
class StopServiceHook : public IHook
{
    PNQ_DECLARE_NON_COPYABLE(StopServiceHook)

public:
    static constexpr std::string_view TYPE_NAME = "stop-service";

    /// @param service_name Service name to stop
    /// @param wait If true, wait for service to reach stopped state
    explicit StopServiceHook(const std::string& service_name, bool wait = true)
        : IHook{std::string{TYPE_NAME}}
        , m_service_name{service_name}
        , m_wait{wait}
    {}

    /// @name Accessors
    /// @{
    const std::string& service_name() const { return m_service_name; }
    bool wait() const { return m_wait; }
    /// @}

private:
    bool execute(const std::unordered_map<std::string, std::string>& variables) const override;

    const std::string m_service_name;
    const bool m_wait;
};

} // namespace insti
