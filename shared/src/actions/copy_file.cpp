#include "pch.h"
#include <insti/actions/copy_file.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <pnq/binary_file.h>

namespace insti
{

    CopyFileAction::CopyFileAction(std::string path, std::string archive_path, std::string description)
        : IAction{std::string{TYPE_NAME}, description.empty() ? "File: " + path : std::move(description)}
        , m_path{std::move(path)}, m_archive_path{std::move(archive_path)}
    {
    }

    bool CopyFileAction::backup(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Backup", description(), -1);

        // Check if source exists
        if (!pnq::file::exists(resolved_path))
        {
            if (ctx->skip_all_errors())
                return true;
            if (cb)
            {
                auto decision = cb->on_error("Source file does not exist", resolved_path);
                return handle_decision(decision, ctx);
            }
            spdlog::warn("Source file does not exist: {}", resolved_path);
            return true;
        }

        // Read file content
        pnq::bytes content;
        if (!pnq::BinaryFile::read(resolved_path, content))
        {
            if (ctx->skip_all_errors())
                return true;
            if (cb)
            {
                auto decision = cb->on_error("Failed to read source file", resolved_path);
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to read source file: {}", resolved_path);
            return false;
        }

        // Write to snapshot
        if (!ctx->writer()->write_binary(m_archive_path, content))
        {
            if (ctx->skip_all_errors())
                return true;
            if (cb)
            {
                auto decision = cb->on_error("Failed to write file to snapshot", m_archive_path);
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write file to snapshot: {}", m_archive_path);
            return false;
        }

        return true;
    }

    bool CopyFileAction::restore(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        if (cb)
            cb->on_progress("Restore", description(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would restore file: {} -> {}", m_archive_path, resolved_path);
            if (cb)
                cb->on_progress("Simulate", std::string("Would restore: ") + resolved_path, -1);
            return true;
        }

        // Check if destination already exists
        if (pnq::file::exists(resolved_path))
        {
            if (cb)
            {
                auto decision = cb->on_file_conflict(resolved_path, "overwrite file");
                if (decision == IActionCallback::Decision::Abort)
                    return false;
                if (decision == IActionCallback::Decision::Skip)
                    return true;
            }
        }

        // Create parent directory if needed
        std::filesystem::path dest(resolved_path);
        if (dest.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(dest.parent_path(), ec);
            if (ec)
            {
                if (ctx->skip_all_errors())
                    return true;
                if (cb)
                {
                    auto decision = cb->on_error("Failed to create parent directory", ec.message());
                    return handle_decision(decision, ctx);
                }
                spdlog::error("Failed to create parent directory: {}", ec.message());
                return false;
            }
        }

        // Read from snapshot
        auto content = ctx->reader()->read_binary(m_archive_path);

        // Write to destination
        if (!pnq::BinaryFile::write(resolved_path, content))
        {
            if (ctx->skip_all_errors())
                return true;
            if (cb)
            {
                auto decision = cb->on_error("Failed to write file", resolved_path);
                return handle_decision(decision, ctx);
            }
            spdlog::error("Failed to write file: {}", resolved_path);
            return false;
        }

        return true;
    }

    bool CopyFileAction::do_clean(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Check if exists
        if (!pnq::file::exists(resolved_path))
            return true;

        // Simulate mode: just log what would happen
        if (simulate)
        {
            spdlog::info("[SIMULATE] Would delete file: {}", resolved_path);
            if (cb)
                cb->on_progress("Simulate", std::string("Would delete: ") + resolved_path, -1);
            return true;
        }

        // Remove file
        std::error_code ec;
        std::filesystem::remove(resolved_path, ec);
        return !ec;
    }

    VerifyResult CopyFileAction::verify(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);

        bool exists_on_system = pnq::file::exists(resolved_path);

        bool exists_in_snapshot = false;
        if (ctx->reader())
            exists_in_snapshot = ctx->reader()->exists(m_archive_path);

        if (!exists_on_system && !exists_in_snapshot)
            return {VerifyResult::Status::Missing, "File not found on system or in snapshot"};

        if (exists_on_system && !exists_in_snapshot && ctx->reader())
            return {VerifyResult::Status::Extra, "File exists on system but not in snapshot"};

        if (!exists_on_system && exists_in_snapshot)
            return {VerifyResult::Status::Missing, "File exists in snapshot but not on system"};

        // Both exist - could compare sizes/hashes
        return {VerifyResult::Status::Match, "File exists"};
    }

    std::vector<std::pair<std::string, std::string>> CopyFileAction::to_params() const
    {
        return {
            {"path", m_path},
            {"archive", m_archive_path}};
    }

} // namespace insti
