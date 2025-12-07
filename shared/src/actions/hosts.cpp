#include "pch.h"
#include <insti/actions/hosts.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <pnq/hosts_file.h>
#include <toml++/toml.hpp>

namespace insti
{

    HostsAction::HostsAction(std::string hostname, std::string archive_path, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty() ? "Hosts: " + hostname : std::move(description)}
        , m_hostname{std::move(hostname)}, m_archive_path{std::move(archive_path)}
    {
    }

    // ============================================================================
    // HostsEntry TOML serialization
    // ============================================================================

    std::string HostsEntry::to_toml() const
    {
        toml::table tbl;
        tbl.insert("ip", ip);
        tbl.insert("hostname", hostname);
        if (!comment.empty())
            tbl.insert("comment", comment);

        std::ostringstream oss;
        oss << tbl;
        return oss.str();
    }

    std::optional<HostsEntry> HostsEntry::from_toml(std::string_view toml_str)
    {
        try
        {
            auto tbl = toml::parse(toml_str);

            HostsEntry entry;
            entry.ip = tbl["ip"].value_or(std::string{});
            entry.hostname = tbl["hostname"].value_or(std::string{});
            entry.comment = tbl["comment"].value_or(std::string{});

            if (entry.ip.empty() || entry.hostname.empty())
                return std::nullopt;

            return entry;
        }
        catch (const toml::parse_error &e)
        {
            spdlog::error("Failed to parse hosts entry TOML: {}", e.what());
            return std::nullopt;
        }
    }

    // ============================================================================
    // HostsAction implementation
    // ============================================================================

    std::string HostsAction::hosts_file_path()
    {
        return pnq::HostsFile::system_path();
    }

    std::optional<HostsEntry> HostsAction::read_entry() const
    {
        pnq::HostsFile hosts;
        if (!hosts.load())
            return std::nullopt;

        auto entry = hosts.find(m_hostname);
        if (!entry)
            return std::nullopt;

        return HostsEntry{entry->ip, entry->hostname, entry->comment};
    }

    bool HostsAction::write_entry(const HostsEntry &entry) const
    {
        pnq::HostsFile hosts;
        if (!hosts.load())
            return false;

        hosts.set(entry.hostname, entry.ip, entry.comment);
        return hosts.save();
    }

    bool HostsAction::delete_entry() const
    {
        pnq::HostsFile hosts;
        if (!hosts.load())
            return true;  // Nothing to delete

        hosts.remove(m_hostname);
        return hosts.save();
    }

    bool HostsAction::backup(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description().c_str(), -1);

        // Read current entry
        auto entry = read_entry();
        if (!entry)
        {
            if (cb)
            {
                auto decision = cb->on_error("Hosts entry does not exist", m_hostname.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::warn("Hosts entry does not exist: {}", m_hostname);
            return true;
        }

        // Write to snapshot as TOML
        if (!ctx->writer()->write_text(m_archive_path, entry->to_toml()))
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to write hosts entry to snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write hosts entry to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool HostsAction::restore(ActionContext *ctx) const
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
            spdlog::info("[SIMULATE] Would restore hosts entry: {}", m_hostname);
            if (cb)
                cb->on_progress("Simulate", std::string("Would restore hosts: ") + m_hostname, -1);
            return true;
        }

        // Read TOML from snapshot
        std::string toml_content = ctx->reader()->read_text(m_archive_path);
        if (toml_content.empty())
        {
            // Empty means delete the entry
            bool success = delete_entry();
            if (!success && cb)
            {
                auto decision = cb->on_error("Failed to delete hosts entry", m_hostname.c_str());
                return handle_decision(decision, ctx);
            }
            return success;
        }

        // Parse entry
        auto entry = HostsEntry::from_toml(toml_content);
        if (!entry)
        {
            if (cb)
            {
                auto decision = cb->on_error("Failed to parse hosts entry from snapshot", m_archive_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to parse hosts entry from snapshot: {}", m_archive_path);
            return false;
        }

        bool success = write_entry(*entry);
        if (!success && cb)
        {
            auto decision = cb->on_error("Failed to write hosts entry", m_hostname.c_str());
            return handle_decision(decision, ctx);
        }

        return success;
    }

    bool HostsAction::do_clean(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would delete hosts entry: {}", m_hostname);
            if (cb)
                cb->on_progress("Simulate", std::string("Would delete hosts: ") + m_hostname, -1);
            return true;
        }

        return delete_entry();
    }

    VerifyResult HostsAction::verify(ActionContext *ctx) const
    {
        // Check if entry exists in hosts file
        auto system_entry = read_entry();
        bool exists_on_system = system_entry.has_value();

        // Check if exists in snapshot (if reader available)
        bool exists_in_snapshot = false;
        std::optional<HostsEntry> snapshot_entry;
        if (ctx->reader() && ctx->reader()->exists(m_archive_path))
        {
            std::string toml_content = ctx->reader()->read_text(m_archive_path);
            snapshot_entry = HostsEntry::from_toml(toml_content);
            exists_in_snapshot = snapshot_entry.has_value();
        }

        if (!exists_on_system && !exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Hosts entry not found on system or in snapshot"};
        }

        if (exists_on_system && !exists_in_snapshot && ctx->reader())
        {
            return {VerifyResult::Status::Extra, "Hosts entry exists on system but not in snapshot"};
        }

        if (!exists_on_system && exists_in_snapshot)
        {
            return {VerifyResult::Status::Missing, "Hosts entry exists in snapshot but not on system"};
        }

        // Both exist - compare IP addresses
        if (system_entry->ip != snapshot_entry->ip)
        {
            return {VerifyResult::Status::Mismatch,
                    "IP mismatch: system='" + system_entry->ip + "' snapshot='" + snapshot_entry->ip + "'"};
        }

        return {VerifyResult::Status::Match, "Hosts entry matches"};
    }

    std::vector<std::pair<std::string, std::string>> HostsAction::to_params() const
    {
        return {
            {"hostname", m_hostname},
            {"archive", m_archive_path}};
    }

} // namespace insti
