#include "pch.h"
#include <insti/hooks/run_process.h>

namespace insti
{

bool RunProcessHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    pnq::string::Expander expander{variables, true};
    expander.expand_dollar(true).expand_percent(true);

    // Resolve path
    std::string resolved_path = expander.expand(m_path);

    // Build command line
    std::string cmdline = "\"" + resolved_path + "\"";
    for (const auto& arg : m_args)
    {
        std::string resolved_arg = expander.expand(arg);
        // Quote arguments that contain spaces
        if (resolved_arg.find(' ') != std::string::npos)
            cmdline += " \"" + resolved_arg + "\"";
        else
            cmdline += " " + resolved_arg;
    }

    spdlog::info("RunProcessHook: cmdline={}", cmdline);
    spdlog::info("RunProcessHook: wait={}, ignore_exit_code={}", m_wait, m_ignore_exit_code);

    // Check if the executable exists
    if (!std::filesystem::exists(resolved_path))
    {
        spdlog::error("RunProcessHook: executable not found: {}", resolved_path);
        return false;
    }

    // Convert to wide string
    std::wstring wide_cmdline = pnq::string::encode_as_utf16(cmdline);

    // Set up process creation
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    // CreateProcess needs a writable buffer for the command line
    std::vector<wchar_t> cmdline_buf(wide_cmdline.begin(), wide_cmdline.end());
    cmdline_buf.push_back(L'\0');

    spdlog::info("RunProcessHook: calling CreateProcessW");

    BOOL created = CreateProcessW(
        nullptr,                    // Application name (use cmdline)
        cmdline_buf.data(),         // Command line
        nullptr,                    // Process attributes
        nullptr,                    // Thread attributes
        FALSE,                      // Inherit handles
        CREATE_NO_WINDOW,           // Creation flags
        nullptr,                    // Environment
        nullptr,                    // Current directory
        &si,
        &pi);

    if (!created)
    {
        DWORD err = GetLastError();
        spdlog::error("RunProcessHook: CreateProcess failed with error {}", err);
        PNQ_LOG_LAST_ERROR("CreateProcess failed");
        return false;
    }

    spdlog::info("RunProcessHook: process created, pid={}", pi.dwProcessId);

    bool success = true;

    if (m_wait)
    {
        spdlog::info("RunProcessHook: waiting for process to complete");
        DWORD wait_result = WaitForSingleObject(pi.hProcess, 30000); // 30 second timeout

        if (wait_result == WAIT_TIMEOUT)
        {
            spdlog::error("RunProcessHook: process timed out after 30 seconds");
            TerminateProcess(pi.hProcess, 1);
            success = false;
        }
        else if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            GetExitCodeProcess(pi.hProcess, &exit_code);
            spdlog::info("RunProcessHook: process exited with code {}", exit_code);

            if (exit_code != 0)
            {
                if (m_ignore_exit_code)
                {
                    spdlog::warn("RunProcessHook: non-zero exit code {} (ignored)", exit_code);
                }
                else
                {
                    spdlog::error("RunProcessHook: non-zero exit code {}", exit_code);
                    success = false;
                }
            }
        }
        else
        {
            spdlog::error("RunProcessHook: WaitForSingleObject returned {}", wait_result);
            success = false;
        }
    }
    else
    {
        spdlog::info("RunProcessHook: not waiting (fire and forget)");
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    spdlog::info("RunProcessHook: returning success={}", success);
    return success;
}

} // namespace insti
