#include "pch.h"
#include <insti/hooks/kill_process.h>
#include <TlHelp32.h>

namespace insti
{

bool KillProcessHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    // Resolve process name
    std::string name = pnq::string::Expander{variables, true}
        .expand_dollar(true)
        .expand_percent(true)
        .expand(m_process_name);

    spdlog::info("Killing process: {}", name);

    // Convert to wide string for comparison
    std::wstring wide_name = pnq::string::encode_as_utf16(name);

    // Take a snapshot of all processes
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        PNQ_LOG_LAST_ERROR("CreateToolhelp32Snapshot failed");
        return false;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    bool found_any = false;
    bool all_succeeded = true;

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            // Case-insensitive comparison
            if (_wcsicmp(pe.szExeFile, wide_name.c_str()) == 0)
            {
                found_any = true;
                DWORD pid = pe.th32ProcessID;

                spdlog::debug("Found process {} with PID {}", name, pid);

                // Open the process
                HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
                if (!proc)
                {
                    PNQ_LOG_LAST_ERROR("OpenProcess failed");
                    all_succeeded = false;
                    continue;
                }

                // Terminate it
                if (!TerminateProcess(proc, 1))
                {
                    PNQ_LOG_LAST_ERROR("TerminateProcess failed");
                    CloseHandle(proc);
                    all_succeeded = false;
                    continue;
                }

                // Wait for it to actually die
                DWORD wait_result = WaitForSingleObject(proc, m_timeout_ms);
                if (wait_result != WAIT_OBJECT_0)
                {
                    spdlog::warn("Process {} did not terminate within timeout", pid);
                    all_succeeded = false;
                }

                CloseHandle(proc);
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);

    if (!found_any)
    {
        spdlog::debug("No process named '{}' found", name);
    }

    // Not finding the process is not an error - it might not be running
    return all_succeeded;
}

} // namespace insti
