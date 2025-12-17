#include "pch.h"
#include <insti/hooks/service.h>
#include <pnq/win32/service.h>

namespace insti
{

bool StartServiceHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    // Resolve service name
    std::string name = pnq::string::Expander{variables, true}
        .expand_dollar(true)
        .expand_percent(true)
        .expand(m_service_name);

    spdlog::info("Starting service: {}", name);

    pnq::win32::SCM scm;
    if (!scm)
    {
        spdlog::error("Failed to open Service Control Manager");
        return false;
    }

    auto svc = scm.open_service(name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc)
    {
        // Service doesn't exist - not necessarily an error
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            spdlog::warn("Service '{}' does not exist", name);
            return true;  // Not an error - service might not be installed
        }
        spdlog::error("Failed to open service '{}': {}", name, err);
        return false;
    }

    if (!svc.start())
    {
        // start() already handles ERROR_SERVICE_ALREADY_RUNNING
        spdlog::error("Failed to start service '{}'", name);
        return false;
    }

    if (m_wait)
    {
        spdlog::debug("Waiting for service '{}' to start...", name);
        if (!svc.wait_until_running())
        {
            spdlog::warn("Service '{}' did not reach running state within timeout", name);
            return false;
        }
        spdlog::info("Service '{}' started successfully", name);
    }

    return true;
}

bool StopServiceHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    // Resolve service name
    std::string name = pnq::string::Expander{variables, true}
        .expand_dollar(true)
        .expand_percent(true)
        .expand(m_service_name);

    spdlog::info("Stopping service: {}", name);

    pnq::win32::SCM scm;
    if (!scm)
    {
        spdlog::error("Failed to open Service Control Manager");
        return false;
    }

    auto svc = scm.open_service(name, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc)
    {
        // Service doesn't exist - not an error for stop
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            spdlog::debug("Service '{}' does not exist (nothing to stop)", name);
            return true;
        }
        spdlog::error("Failed to open service '{}': {}", name, err);
        return false;
    }

    if (!svc.stop())
    {
        // stop() already handles ERROR_SERVICE_NOT_ACTIVE
        spdlog::error("Failed to stop service '{}'", name);
        return false;
    }

    if (m_wait)
    {
        spdlog::debug("Waiting for service '{}' to stop...", name);
        if (!svc.wait_until_stopped())
        {
            spdlog::warn("Service '{}' did not reach stopped state within timeout", name);
            return false;
        }
        spdlog::info("Service '{}' stopped successfully", name);
    }

    return true;
}

} // namespace insti
