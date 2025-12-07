#include "pch.h"
#include <insti/actions/environment.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <format>

namespace insti
{

    EnvironmentAction::EnvironmentAction(std::string name, EnvironmentScope scope, std::string archive_path, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty()
                                              ? "Environment: " + name + (scope == EnvironmentScope::User ? " (user)" : " (system)")
                                              : std::move(description)}
        , m_name{std::move(name)}, m_scope{scope}, m_archive_path{std::move(archive_path)}
    {
    }

    // Broadcast environment change to all windows
    static void broadcast_environment_change()
    {
        // WM_SETTINGCHANGE with "Environment" tells Explorer and other apps to reload
        DWORD_PTR result;
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            reinterpret_cast<LPARAM>(L"Environment"),
            SMTO_ABORTIFHUNG,
            5000,
            &result);
    }

    // Open environment registry key
    static HKEY open_env_key(EnvironmentScope scope, REGSAM access)
    {
        HKEY root = (scope == EnvironmentScope::User) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        const wchar_t *subkey = (scope == EnvironmentScope::User)
                                    ? L"Environment"
                                    : L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

        HKEY hkey = nullptr;
        LONG result = RegOpenKeyExW(root, subkey, 0, access, &hkey);
        if (result != ERROR_SUCCESS)
        {
            spdlog::warn("RegOpenKeyEx failed: {}", result);
            return nullptr;
        }
        return hkey;
    }

    std::string EnvironmentAction::read_value() const
    {
        HKEY hkey = open_env_key(m_scope, KEY_READ);
        if (!hkey)
            return {};

        std::wstring wide_name = pnq::string::encode_as_utf16(m_name);

        // First get size
        DWORD type = 0;
        DWORD size = 0;
        LONG result = RegQueryValueExW(hkey, wide_name.c_str(), nullptr, &type, nullptr, &size);
        if (result != ERROR_SUCCESS)
        {
            RegCloseKey(hkey);
            return {};
        }

        // Read value
        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
        result = RegQueryValueExW(hkey, wide_name.c_str(), nullptr, &type,
                                  reinterpret_cast<LPBYTE>(buffer.data()), &size);
        RegCloseKey(hkey);

        if (result != ERROR_SUCCESS)
            return {};

        return pnq::string::encode_as_utf8(buffer.data());
    }

    bool EnvironmentAction::write_value(const std::string &value) const
    {
        HKEY hkey = open_env_key(m_scope, KEY_WRITE);
        if (!hkey)
        {
            spdlog::error("Failed to open environment registry key for writing");
            return false;
        }

        std::wstring wide_name = pnq::string::encode_as_utf16(m_name);
        std::wstring wide_value = pnq::string::encode_as_utf16(value);

        // Use REG_EXPAND_SZ if value contains %VAR% references, otherwise REG_SZ
        DWORD type = (value.find('%') != std::string::npos) ? REG_EXPAND_SZ : REG_SZ;
        DWORD size = static_cast<DWORD>((wide_value.size() + 1) * sizeof(wchar_t));

        LONG result = RegSetValueExW(hkey, wide_name.c_str(), 0, type,
                                     reinterpret_cast<const BYTE *>(wide_value.c_str()), size);
        RegCloseKey(hkey);

        if (result != ERROR_SUCCESS)
        {
            spdlog::error("Failed to write environment variable {}: {}", m_name, result);
            return false;
        }

        broadcast_environment_change();
        return true;
    }

    bool EnvironmentAction::delete_value() const
    {
        HKEY hkey = open_env_key(m_scope, KEY_WRITE);
        if (!hkey)
        {
            // If we can't open, assume value doesn't exist
            return true;
        }

        std::wstring wide_name = pnq::string::encode_as_utf16(m_name);
        LONG result = RegDeleteValueW(hkey, wide_name.c_str());
        RegCloseKey(hkey);

        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        {
            spdlog::error("Failed to delete environment variable {}: {}", m_name, result);
            return false;
        }

        broadcast_environment_change();
        return true;
    }

    bool EnvironmentAction::backup(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description().c_str(), -1);

        // Read current value
        std::string value = read_value();

        // Write to snapshot (even if empty - means "not set")
        if (!ctx->writer()->write_text(m_archive_path, value))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write environment variable to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write environment variable to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool EnvironmentAction::restore(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        if (cb)
            cb->on_progress("Restore", description().c_str(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would restore environment variable: {}", m_name);
            if (cb)
                cb->on_progress("Simulate", std::string("Would restore env: ") + m_name, -1);
            return true;
        }

        // Read value from snapshot
        std::string value = ctx->reader()->read_text(m_archive_path);

        bool success = value.empty() ? delete_value() : write_value(value);
        if (!success && cb)
        {
            auto decision = cb->on_error("Failed to restore environment variable", m_name.c_str());
            return handle_decision(decision, ctx);
        }

        return success;
    }

    bool EnvironmentAction::do_clean(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would delete environment variable: {}", m_name);
            if (cb)
                cb->on_progress("Simulate", std::string("Would delete env: ") + m_name, -1);
            return true;
        }

        return delete_value();
    }

    VerifyResult EnvironmentAction::verify(ActionContext *ctx) const
    {
        // Read current value from system
        std::string current_value = read_value();
        bool exists_on_system = !current_value.empty();

        // Check if exists in snapshot (if reader available)
        bool exists_in_snapshot = false;
        std::string snapshot_value;
        if (ctx->reader() && ctx->reader()->exists(m_archive_path))
        {
            snapshot_value = ctx->reader()->read_text(m_archive_path);
            exists_in_snapshot = !snapshot_value.empty();
        }

        if (!exists_on_system && !exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Environment variable not set on system or in snapshot"};
        }

        if (exists_on_system && !exists_in_snapshot && ctx->reader())
        {
            return {VerifyResult::Status::Extra, "Environment variable set on system but not in snapshot"};
        }

        if (!exists_on_system && exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Environment variable in snapshot but not set on system"};
        }

        // Both exist - compare values
        if (current_value != snapshot_value && ctx->reader())
        {
            return {VerifyResult::Status::Mismatch, std::format("Value mismatch: system='{}' snapshot='{}'", current_value, snapshot_value)};
        }

        return {VerifyResult::Status::Match, "Environment variable matches"};
    }

    std::vector<std::pair<std::string, std::string>> EnvironmentAction::to_params() const
    {
        return {
            {"name", m_name},
            {"scope", m_scope == EnvironmentScope::User ? "user" : "system"},
            {"archive", m_archive_path}};
    }

} // namespace insti
