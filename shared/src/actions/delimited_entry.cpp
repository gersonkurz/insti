#include "pch.h"
#include <insti/actions/delimited_entry.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>

namespace insti
{

    DelimitedEntryAction::DelimitedEntryAction(std::string key, std::string value_name, std::string entry,
                                               std::string archive_path, std::string delimiter,
                                               InsertPosition insert_pos, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty()
                                              ? "List entry: " + value_name + " in " + key
                                              : std::move(description)}
        , m_key{std::move(key)}, m_value_name{std::move(value_name)}, m_entry{std::move(entry)}
        , m_archive_path{std::move(archive_path)}, m_delimiter{std::move(delimiter)}, m_insert_pos{insert_pos}
    {
    }

    std::string DelimitedEntryAction::read_value() const
    {
        pnq::regis3::key reg_key{m_key};
        if (!reg_key.open_for_reading())
            return {};

        return reg_key.get_string(m_value_name);
    }

    bool DelimitedEntryAction::write_value(const std::string &value) const
    {
        pnq::regis3::key reg_key{m_key};
        if (!reg_key.open_for_writing())
        {
            PNQ_LOG_LAST_ERROR("Failed to open registry key for writing");
            return false;
        }

        // Use REG_EXPAND_SZ if value contains %VAR% references, otherwise REG_SZ
        DWORD type = (value.find('%') != std::string::npos) ? REG_EXPAND_SZ : REG_SZ;
        std::wstring wide_name = pnq::string::encode_as_utf16(m_value_name);
        std::wstring wide_value = pnq::string::encode_as_utf16(value);
        DWORD size = static_cast<DWORD>((wide_value.size() + 1) * sizeof(wchar_t));

        LONG result = RegSetValueExW(reg_key.handle(), wide_name.c_str(), 0, type,
                                     reinterpret_cast<const BYTE *>(wide_value.c_str()), size);
        if (result != ERROR_SUCCESS)
        {
            PNQ_LOG_WIN_ERROR(result, "Failed to write registry value");
            return false;
        }

        return true;
    }

    bool DelimitedEntryAction::is_in_list() const
    {
        const std::string value = read_value();
        const auto entries = pnq::string::split_stripped(value, m_delimiter);

        for (const auto &e : entries)
        {
            if (pnq::string::equals_nocase(e, m_entry))
                return true;
        }
        return false;
    }

    bool DelimitedEntryAction::add_to_list() const
    {
        if (is_in_list())
        {
            spdlog::debug("Entry already in list: {}", m_entry);
            return true;
        }

        const std::string value = read_value();
        auto entries = pnq::string::split_stripped(value, m_delimiter);

        if (m_insert_pos == InsertPosition::Prepend)
            entries.insert(entries.begin(), m_entry);
        else
            entries.push_back(m_entry);

        return write_value(pnq::string::join(entries, m_delimiter));
    }

    bool DelimitedEntryAction::remove_from_list() const
    {
        const std::string value = read_value();
        auto entries = pnq::string::split_stripped(value, m_delimiter);

        auto it = std::find_if(entries.begin(), entries.end(),
                               [this](const std::string &e)
                               { return pnq::string::equals_nocase(e, m_entry); });

        if (it == entries.end())
        {
            spdlog::debug("Entry not in list: {}", m_entry);
            return true;
        }

        entries.erase(it);
        return write_value(pnq::string::join(entries, m_delimiter));
    }

    bool DelimitedEntryAction::backup(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description(), -1);

        // Check if entry is currently in list
        const bool present = is_in_list();

        // Write status to snapshot ("present" or "absent")
        const std::string status = present ? "present" : "absent";
        if (!ctx->writer()->write_text(m_archive_path, status))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write list entry status to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write list entry status to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool DelimitedEntryAction::restore(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Restore", description(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Read status from snapshot
        const std::string status = ctx->reader()->read_text(m_archive_path);

        const bool success = (status == "present") ? add_to_list() : remove_from_list();
        if (!success && cb)
        {
            auto decision = cb->on_error("Failed to modify list", m_entry.c_str());
            return handle_decision(decision, ctx);
        }

        return success;
    }

    bool DelimitedEntryAction::do_clean(ActionContext */*ctx*/) const
    {
        return remove_from_list();
    }

    VerifyResult DelimitedEntryAction::verify(ActionContext *ctx) const
    {
        const bool on_system = is_in_list();

        // Check snapshot status
        bool in_snapshot_as_present = false;
        if (ctx->reader() && ctx->reader()->exists(m_archive_path))
        {
            const std::string status = ctx->reader()->read_text(m_archive_path);
            in_snapshot_as_present = (status == "present");
        }

        if (on_system && in_snapshot_as_present)
        {
            return {VerifyResult::Status::Match, "List entry present (as expected)"};
        }

        if (!on_system && !in_snapshot_as_present)
        {
            return {VerifyResult::Status::Match, "List entry absent (as expected)"};
        }

        if (on_system && !in_snapshot_as_present)
        {
            return {VerifyResult::Status::Mismatch, "List entry present on system but marked absent in snapshot"};
        }

        // !on_system && in_snapshot_as_present
        return {VerifyResult::Status::Mismatch, "List entry absent on system but marked present in snapshot"};
    }

    std::vector<std::pair<std::string, std::string>> DelimitedEntryAction::to_params() const
    {
        return {
            {"key", m_key},
            {"value", m_value_name},
            {"entry", m_entry},
            {"delimiter", m_delimiter},
            {"insert", m_insert_pos == InsertPosition::Prepend ? "prepend" : "append"},
            {"archive", m_archive_path}};
    }

} // namespace insti
