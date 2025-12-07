#include "pch.h"
#include <insti/actions/multistring_entry.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>

namespace insti
{

    MultiStringEntryAction::MultiStringEntryAction(std::string key, std::string value_name, std::string entry,
                                                   std::string archive_path, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty()
                                              ? "Multi-string entry: " + value_name + " in " + key
                                              : std::move(description)}
        , m_key{std::move(key)}, m_value_name{std::move(value_name)}, m_entry{std::move(entry)}
        , m_archive_path{std::move(archive_path)}
    {
    }

    std::vector<std::string> MultiStringEntryAction::read_multi_string() const
    {
        std::vector<std::string> entries;

        pnq::regis3::key reg_key{m_key};
        if (!reg_key.open_for_reading())
            return entries;

        // Get the raw data
        DWORD type = 0;
        DWORD size = 0;
        std::wstring wide_name = pnq::string::encode_as_utf16(m_value_name);

        if (RegQueryValueExW(reg_key.handle(), wide_name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
            return entries;

        if (type != REG_MULTI_SZ)
        {
            spdlog::warn("Registry value {} is not REG_MULTI_SZ (type={})", m_value_name, type);
            return entries;
        }

        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
        if (RegQueryValueExW(reg_key.handle(), wide_name.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS)
            return entries;

        // Parse null-separated strings (double-null terminated)
        const wchar_t *p = buffer.data();
        while (*p)
        {
            entries.push_back(pnq::string::encode_as_utf8(p));
            p += wcslen(p) + 1;
        }

        return entries;
    }

    bool MultiStringEntryAction::write_multi_string(const std::vector<std::string> &entries) const
    {
        pnq::regis3::key reg_key{m_key};
        if (!reg_key.open_for_writing())
        {
            PNQ_LOG_LAST_ERROR("Failed to open registry key for writing");
            return false;
        }

        // Build double-null-terminated wide string
        std::wstring data;
        for (const auto &entry : entries)
        {
            data += pnq::string::encode_as_utf16(entry);
            data += L'\0';
        }
        data += L'\0'; // Final null terminator

        std::wstring wide_name = pnq::string::encode_as_utf16(m_value_name);
        DWORD size = static_cast<DWORD>(data.size() * sizeof(wchar_t));

        LONG result = RegSetValueExW(reg_key.handle(), wide_name.c_str(), 0, REG_MULTI_SZ,
                                     reinterpret_cast<const BYTE *>(data.c_str()), size);
        if (result != ERROR_SUCCESS)
        {
            PNQ_LOG_WIN_ERROR(result, "Failed to write REG_MULTI_SZ value");
            return false;
        }

        return true;
    }

    bool MultiStringEntryAction::is_in_list() const
    {
        auto entries = read_multi_string();

        for (const auto &e : entries)
        {
            if (pnq::string::equals_nocase(e, m_entry))
                return true;
        }
        return false;
    }

    bool MultiStringEntryAction::add_to_list() const
    {
        if (is_in_list())
        {
            spdlog::debug("Entry already in multi-string: {}", m_entry);
            return true;
        }

        auto entries = read_multi_string();
        entries.push_back(m_entry);

        return write_multi_string(entries);
    }

    bool MultiStringEntryAction::remove_from_list() const
    {
        auto entries = read_multi_string();

        // Remove matching entries (case-insensitive)
        std::vector<std::string> filtered;
        for (const auto &e : entries)
        {
            if (!pnq::string::equals_nocase(e, m_entry))
                filtered.push_back(e);
        }

        if (filtered.size() == entries.size())
        {
            spdlog::debug("Entry not in multi-string: {}", m_entry);
            return true; // Nothing to remove
        }

        return write_multi_string(filtered);
    }

    bool MultiStringEntryAction::backup(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description(), -1);

        // Check if entry is currently in list
        bool present = is_in_list();

        // Write status to snapshot ("present" or "absent")
        std::string status = present ? "present" : "absent";
        if (!ctx->writer()->write_text(m_archive_path, status))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write multi-string entry status to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write multi-string entry status to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool MultiStringEntryAction::restore(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Restore", description(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Read status from snapshot
        std::string status = ctx->reader()->read_text(m_archive_path);

        bool success = (status == "present") ? add_to_list() : remove_from_list();
        if (!success && cb)
        {
            auto decision = cb->on_error("Failed to modify multi-string", m_entry.c_str());
            return handle_decision(decision, ctx);
        }

        return success;
    }

    bool MultiStringEntryAction::do_clean(ActionContext */*ctx*/) const
    {
        return remove_from_list();
    }

    VerifyResult MultiStringEntryAction::verify(ActionContext *ctx) const
    {
        bool on_system = is_in_list();

        // Check snapshot status
        bool in_snapshot_as_present = false;
        if (ctx->reader() && ctx->reader()->exists(m_archive_path))
        {
            std::string status = ctx->reader()->read_text(m_archive_path);
            in_snapshot_as_present = (status == "present");
        }

        if (on_system && in_snapshot_as_present)
        {
            return {VerifyResult::Status::Match, "Multi-string entry present (as expected)"};
        }

        if (!on_system && !in_snapshot_as_present)
        {
            return {VerifyResult::Status::Match, "Multi-string entry absent (as expected)"};
        }

        if (on_system && !in_snapshot_as_present)
        {
            return {VerifyResult::Status::Mismatch, "Multi-string entry present on system but marked absent in snapshot"};
        }

        // !on_system && in_snapshot_as_present
        return {VerifyResult::Status::Mismatch, "Multi-string entry absent on system but marked present in snapshot"};
    }

    std::vector<std::pair<std::string, std::string>> MultiStringEntryAction::to_params() const
    {
        return {
            {"key", m_key},
            {"value", m_value_name},
            {"entry", m_entry},
            {"archive", m_archive_path}};
    }

} // namespace insti
