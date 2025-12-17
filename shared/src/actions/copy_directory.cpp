#include "pch.h"
#include <insti/actions/copy_directory.h>
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <fstream>
#include <unordered_set>
#include <unordered_map>

namespace insti
{

    bool CopyDirectoryAction::matches_filters(std::string_view filename) const
    {
        // First check include filters (whitelist)
        // If include filters exist, file must match at least one
        if (!m_include_filters.empty())
        {
            bool included = false;
            for (const auto &pattern : m_include_filters)
            {
                if (pnq::file::match(pattern, filename))
                {
                    included = true;
                    break;
                }
            }
            if (!included)
                return false;
        }

        // Then check exclude filters (blacklist on top of whitelist)
        for (const auto &pattern : m_exclude_filters)
        {
            if (pnq::file::match(pattern, filename))
                return false;
        }

        return true;
    }

    std::optional<CopyDirectoryAction::CollectedEntries> CopyDirectoryAction::collect_entries(
        const std::filesystem::path &base, ActionContext *ctx) const
    {
        CollectedEntries result;
        auto *cb = ctx->callback();
        std::error_code ec;

        spdlog::info("collect_entries: starting iteration of {}, recursive={}", base.string(), m_recursive);

        auto iterator = m_recursive
            ? std::filesystem::recursive_directory_iterator(base, ec)
            : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::none, ec);

        if (ec)
        {
            spdlog::error("collect_entries: failed to create iterator: {}", ec.message());
            return std::nullopt;
        }

        int count = 0;
        for (const auto &entry : iterator)
        {
            if (ec)
            {
                if (ctx->skip_all_errors())
                {
                    ec.clear();
                    continue;
                }

                if (cb)
                {
                    auto decision = cb->on_error("Error iterating directory", ec.message().c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        // Can't retry iteration mid-stream, treat as skip
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        ec.clear();
                        continue;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        ec.clear();
                        continue;
                    case IActionCallback::Decision::Abort:
                    default:
                        return std::nullopt;
                    }
                }
                else
                {
                    spdlog::error("Error iterating directory: {}", ec.message());
                    return std::nullopt;
                }
            }

            if (entry.is_directory())
            {
                result.dirs.push_back(entry.path());
            }
            else if (entry.is_regular_file())
            {
                // Always exclude blueprint.xml files (instance blueprints shouldn't be captured as artifacts)
                std::string filename = entry.path().filename().string();
                if (pnq::string::equals_nocase(filename, "blueprint.xml"))
                    continue;

                if (matches_filters(filename))
                    result.files.push_back(entry.path());
            }

            ++count;
            if (count % 100 == 0)
                spdlog::debug("collect_entries: processed {} entries so far", count);
        }

        spdlog::info("collect_entries: iteration complete, {} total entries", count);
        return result;
    }

    bool CopyDirectoryAction::backup_empty_directories(
        const std::filesystem::path &base, const CollectedEntries &entries,
        std::string_view archive_prefix, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        auto *writer = ctx->writer();
        std::error_code ec;

        // Pre-compute set of directories that contain files (O(n) instead of O(n²))
        std::unordered_set<std::string> dirs_with_files;
        for (const auto &file : entries.files)
        {
            // Add all parent directories of this file
            std::filesystem::path parent = file.parent_path();
            while (parent != base && !parent.empty())
            {
                dirs_with_files.insert(parent.string());
                parent = parent.parent_path();
            }
        }

        for (const auto &dir : entries.dirs)
        {
            // Skip if directory contains files
            if (dirs_with_files.count(dir.string()) > 0)
                continue;

            auto rel = std::filesystem::relative(dir, base, ec);
            if (ec)
            {
                spdlog::error("Error computing relative path: {}", ec.message());
                continue;
            }

            std::string dir_rel_str = rel.string();
            std::replace(dir_rel_str.begin(), dir_rel_str.end(), '\\', '/');
            std::string dest_path = std::string{archive_prefix} + "/" + dir_rel_str;

            // Retry loop
            while (true)
            {
                if (writer->create_directory(dest_path))
                    break;

                if (ctx->skip_all_errors())
                    break;

                if (cb)
                {
                    auto decision = cb->on_error("Failed to create directory in archive", dest_path.c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to create directory in archive: {}", dest_path);
                    return false;
                }
            }
        }

        return true;
    }

    bool CopyDirectoryAction::backup_files(
        const std::filesystem::path &base, const std::vector<std::filesystem::path> &files,
        std::string_view archive_prefix, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        auto *writer = ctx->writer();
        std::error_code ec;

        const size_t total = files.size();
        int last_percent = -1;

        for (size_t i = 0; i < total; ++i)
        {
            const auto &file = files[i];

            // Progress reporting at 1% thresholds
            if (cb && total > 0)
            {
                int percent = static_cast<int>((i * 100) / total);
                if (percent >= last_percent + 1)
                {
                    cb->on_progress("Backup", file.filename().string(), percent);
                    last_percent = percent;
                }
            }

            auto rel = std::filesystem::relative(file, base, ec);
            if (ec)
            {
                spdlog::error("Error computing relative path: {}", ec.message());
                continue;
            }

            std::string rel_str = rel.string();
            std::replace(rel_str.begin(), rel_str.end(), '\\', '/');
            std::string dest_path = std::string{archive_prefix} + "/" + rel_str;
            std::string src_path = file.string();

            // Retry loop
            while (true)
            {
                if (writer->write_file(dest_path, src_path))
                    break;

                if (ctx->skip_all_errors())
                    break;

                if (cb)
                {
                    auto decision = cb->on_error("Failed to backup file", src_path.c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to backup file: {}", src_path);
                    return false;
                }
            }
        }

        return true;
    }

    bool CopyDirectoryAction::backup(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        auto *cb = ctx->callback();

        spdlog::info("CopyDirectoryAction::backup: path={}, archive={}", resolved_path, m_archive_path);

        if (cb)
            cb->on_progress("Backup", description().c_str(), -1);

        // Check if source exists
        std::filesystem::path base{resolved_path};
        spdlog::info("CopyDirectoryAction::backup: checking if source exists");
        if (!std::filesystem::exists(base))
        {
            spdlog::warn("CopyDirectoryAction::backup: source does not exist: {}", resolved_path);
            if (ctx->skip_all_errors())
                return true;
            if (cb)
            {
                auto decision = cb->on_error("Source directory does not exist", resolved_path.c_str());
                return handle_decision(decision, ctx);
            }
            spdlog::warn("Source directory does not exist: {}", resolved_path);
            return true;
        }

        // Normalize archive prefix
        std::string prefix = m_archive_path;
        if (!prefix.empty() && prefix.back() == '/')
            prefix.pop_back();

        // Collect entries
        spdlog::info("CopyDirectoryAction::backup: collecting entries from {}", base.string());
        auto entries = collect_entries(base, ctx);
        if (!entries)
        {
            spdlog::error("CopyDirectoryAction::backup: collect_entries failed");
            return false;
        }
        spdlog::info("CopyDirectoryAction::backup: collected {} dirs, {} files", entries->dirs.size(), entries->files.size());

        // Create empty directories
        spdlog::info("CopyDirectoryAction::backup: backing up empty directories");
        if (!backup_empty_directories(base, *entries, prefix, ctx))
        {
            spdlog::error("CopyDirectoryAction::backup: backup_empty_directories failed");
            return false;
        }

        // Backup files with progress
        spdlog::info("CopyDirectoryAction::backup: backing up {} files", entries->files.size());
        if (!backup_files(base, entries->files, prefix, ctx))
        {
            spdlog::error("CopyDirectoryAction::backup: backup_files failed");
            return false;
        }

        spdlog::info("CopyDirectoryAction::backup: completed successfully");
        if (cb)
            cb->on_progress("Backup", description().c_str(), 100);

        return true;
    }

    CopyDirectoryAction::ArchiveEntries CopyDirectoryAction::collect_archive_entries(
        std::string_view archive_prefix, ActionContext *ctx) const
    {
        ArchiveEntries result;
        auto *reader = ctx->reader();

        // Normalize prefix (remove trailing slash)
        std::string prefix{archive_prefix};
        if (!prefix.empty() && prefix.back() == '/')
            prefix.pop_back();

        // Get all paths and filter by prefix
        auto all_paths = reader->get_all_paths();
        std::string prefix_with_slash = prefix + "/";

        for (const auto &path : all_paths)
        {
            if (!path.starts_with(prefix_with_slash))
                continue;

            // Get relative path (after prefix/)
            std::string rel_path = path.substr(prefix_with_slash.length());
            if (rel_path.empty())
                continue;

            if (reader->is_directory(path))
                result.dirs.push_back(rel_path);
            else
                result.files.push_back(rel_path);
        }

        // Sort directories by depth (shallowest first for creation)
        std::sort(result.dirs.begin(), result.dirs.end(),
            [](const std::string &a, const std::string &b) {
                return std::count(a.begin(), a.end(), '/') < std::count(b.begin(), b.end(), '/');
            });

        return result;
    }

    bool CopyDirectoryAction::restore_directories(
        const std::filesystem::path &dest_base,
        const std::vector<std::string> &rel_dirs, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        for (const auto &rel_dir : rel_dirs)
        {
            std::filesystem::path dest_path = dest_base / rel_dir;

            // Simulate mode: just log what would happen
            if (simulate)
            {
                spdlog::info("[SIMULATE] Would create directory: {}", dest_path.string());
                if (cb)
                    cb->on_progress("Simulate", std::string("Would create dir: ") + dest_path.string(), -1);
                continue;
            }

            // Retry loop
            while (true)
            {
                std::error_code ec;
                std::filesystem::create_directories(dest_path, ec);
                if (!ec)
                    break; // Success

                if (ctx->skip_all_errors())
                    break;

                if (cb)
                {
                    auto decision = cb->on_error("Failed to create directory", (dest_path.string() + ": " + ec.message()).c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to create directory: {} - {}", dest_path.string(), ec.message());
                    return false;
                }
            }
        }

        return true;
    }

    bool CopyDirectoryAction::restore_files(
        std::string_view archive_prefix, const std::filesystem::path &dest_base,
        const std::vector<std::string> &rel_files, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        auto *reader = ctx->reader();
        const bool simulate = ctx->simulate();

        // Normalize prefix
        std::string prefix{archive_prefix};
        if (!prefix.empty() && prefix.back() == '/')
            prefix.pop_back();

        const size_t total = rel_files.size();
        int last_percent = -1;

        for (size_t i = 0; i < total; ++i)
        {
            const auto &rel_file = rel_files[i];

            // Progress reporting at 1% thresholds
            if (cb && total > 0)
            {
                int percent = static_cast<int>((i * 100) / total);
                if (percent >= last_percent + 1)
                {
                    // Extract just filename for progress display
                    size_t last_slash = rel_file.rfind('/');
                    std::string filename = (last_slash != std::string::npos)
                        ? rel_file.substr(last_slash + 1)
                        : rel_file;
                    cb->on_progress("Restore", filename, percent);
                    last_percent = percent;
                }
            }

            std::string archive_path = prefix + "/" + rel_file;
            std::filesystem::path dest_path = dest_base / rel_file;

            // Simulate mode: just log what would happen
            if (simulate)
            {
                spdlog::info("[SIMULATE] Would extract: {} -> {}", archive_path, dest_path.string());
                if (cb)
                    cb->on_progress("Simulate", std::string("Would extract: ") + dest_path.string(), -1);
                continue;
            }

            // Ensure parent directory exists
            if (dest_path.has_parent_path())
            {
                std::error_code ec;
                std::filesystem::create_directories(dest_path.parent_path(), ec);
                // Ignore errors here - will fail on extract if truly problematic
            }

            // Retry loop
            while (true)
            {
                if (reader->extract_to_file(archive_path, dest_path.string()))
                    break; // Success

                if (ctx->skip_all_errors())
                    break;

                if (cb)
                {
                    auto decision = cb->on_error("Failed to extract file", dest_path.string().c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to extract file: {}", dest_path.string());
                    return false;
                }
            }
        }

        return true;
    }

    bool CopyDirectoryAction::restore(ActionContext *ctx) const
    {
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Restore", description().c_str(), -1);

        if (!check_archive_exists(m_archive_path, ctx))
            return true;

        // Check if destination already exists
        if (pnq::directory::exists(resolved_path))
        {
            if (cb)
            {
                auto decision = cb->on_file_conflict(resolved_path.c_str(), "overwrite directory");
                if (decision == IActionCallback::Decision::Abort)
                    return false;
                if (decision == IActionCallback::Decision::Skip)
                    return true;
                // Continue = overwrite
            }
        }

        // Create destination directory
        std::filesystem::path dest_base{resolved_path};
        {
            std::error_code ec;
            std::filesystem::create_directories(dest_base, ec);
            if (ec)
            {
                if (ctx->skip_all_errors())
                    return true;
                if (cb)
                {
                    auto decision = cb->on_error("Failed to create destination directory", ec.message().c_str());
                    return handle_decision(decision, ctx);
                }
                spdlog::error("Failed to create destination directory: {}", ec.message());
                return false;
            }
        }

        // Collect archive entries
        auto entries = collect_archive_entries(m_archive_path, ctx);

        // Create directories first
        if (!restore_directories(dest_base, entries.dirs, ctx))
            return false;

        // Extract files with progress
        if (!restore_files(m_archive_path, dest_base, entries.files, ctx))
            return false;

        if (cb)
            cb->on_progress("Restore", description().c_str(), 100);

        return true;
    }

    bool CopyDirectoryAction::clean_files(
        const std::vector<std::filesystem::path> &files, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();
        const size_t total = files.size();
        int last_percent = -1;

        for (size_t i = 0; i < total; ++i)
        {
            const auto &file = files[i];

            // Progress reporting at 1% thresholds
            if (cb && total > 0)
            {
                int percent = static_cast<int>((i * 100) / total);
                if (percent >= last_percent + 1)
                {
                    cb->on_progress("Clean", file.filename().string(), percent);
                    last_percent = percent;
                }
            }

            // Simulate mode: just log what would happen
            if (simulate)
            {
                std::error_code ec;
                if (std::filesystem::exists(file, ec))
                {
                    spdlog::info("[SIMULATE] Would delete file: {}", file.string());
                    if (cb)
                        cb->on_progress("Simulate", std::string("Would delete: ") + file.string(), -1);
                }
                continue;
            }

            // Retry loop
            while (true)
            {
                std::error_code ec;
                if (!std::filesystem::exists(file, ec))
                    break; // Already gone

                std::filesystem::remove(file, ec);
                if (!ec)
                    break; // Success

                if (ctx->skip_all_errors())
                    break; // Skip without asking

                if (cb)
                {
                    auto decision = cb->on_error("Failed to delete file", (file.string() + ": " + ec.message()).c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to delete file: {} - {}", file.string(), ec.message());
                    return false;
                }
            }
        }

        return true;
    }

    bool CopyDirectoryAction::clean_directories(
        const std::filesystem::path &base,
        const std::vector<std::filesystem::path> &dirs, ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const bool simulate = ctx->simulate();

        // Sort directories by depth (deepest first) so we delete children before parents
        std::vector<std::filesystem::path> sorted_dirs = dirs;
        std::sort(sorted_dirs.begin(), sorted_dirs.end(),
            [](const std::filesystem::path &a, const std::filesystem::path &b) {
                return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
            });

        for (const auto &dir : sorted_dirs)
        {
            // Simulate mode: just log what would happen
            if (simulate)
            {
                std::error_code ec;
                if (std::filesystem::exists(dir, ec))
                {
                    spdlog::info("[SIMULATE] Would delete directory: {}", dir.string());
                    if (cb)
                        cb->on_progress("Simulate", std::string("Would delete dir: ") + dir.string(), -1);
                }
                continue;
            }

            // Retry loop
            while (true)
            {
                std::error_code ec;
                if (!std::filesystem::exists(dir, ec))
                    break; // Already gone

                std::filesystem::remove(dir, ec); // remove (not remove_all) - should be empty
                if (!ec)
                    break; // Success

                if (ctx->skip_all_errors())
                    break;

                if (cb)
                {
                    auto decision = cb->on_error("Failed to delete directory", (dir.string() + ": " + ec.message()).c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                        continue;
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        break;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        break;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                    break;
                }
                else
                {
                    spdlog::error("Failed to delete directory: {} - {}", dir.string(), ec.message());
                    return false;
                }
            }
        }

        // Finally, try to remove the base directory itself
        if (simulate)
        {
            std::error_code ec;
            if (std::filesystem::exists(base, ec))
            {
                spdlog::info("[SIMULATE] Would delete base directory: {}", base.string());
                if (cb)
                    cb->on_progress("Simulate", std::string("Would delete dir: ") + base.string(), -1);
            }
            return true;
        }

        while (true)
        {
            std::error_code ec;
            if (!std::filesystem::exists(base, ec))
                break;

            std::filesystem::remove(base, ec);
            if (!ec)
                break;

            if (ctx->skip_all_errors())
                break;

            if (cb)
            {
                auto decision = cb->on_error("Failed to delete base directory", (base.string() + ": " + ec.message()).c_str());
                switch (decision)
                {
                case IActionCallback::Decision::Retry:
                    continue;
                case IActionCallback::Decision::Skip:
                case IActionCallback::Decision::Continue:
                    break;
                case IActionCallback::Decision::SkipAll:
                    ctx->set_skip_all_errors(true);
                    break;
                case IActionCallback::Decision::Abort:
                default:
                    return false;
                }
                break;
            }
            else
            {
                spdlog::error("Failed to delete base directory: {} - {}", base.string(), ec.message());
                return false;
            }
        }

        return true;
    }

    bool CopyDirectoryAction::do_clean(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);

        std::filesystem::path base{resolved_path};

        // Check if exists
        if (!std::filesystem::exists(base))
            return true; // Already clean

        // Collect entries (reuse backup logic, but ignore filters for clean - delete everything)
        // Note: For clean we don't apply filters - we delete everything in the directory
        CollectedEntries entries;
        std::error_code ec;

        auto iterator = m_recursive
            ? std::filesystem::recursive_directory_iterator(base, ec)
            : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::none, ec);

        for (const auto &entry : iterator)
        {
            if (ec)
            {
                if (ctx->skip_all_errors())
                {
                    ec.clear();
                    continue;
                }

                if (cb)
                {
                    auto decision = cb->on_error("Error iterating directory", ec.message().c_str());
                    switch (decision)
                    {
                    case IActionCallback::Decision::Retry:
                    case IActionCallback::Decision::Skip:
                    case IActionCallback::Decision::Continue:
                        ec.clear();
                        continue;
                    case IActionCallback::Decision::SkipAll:
                        ctx->set_skip_all_errors(true);
                        ec.clear();
                        continue;
                    case IActionCallback::Decision::Abort:
                    default:
                        return false;
                    }
                }
                else
                {
                    spdlog::error("Error iterating directory: {}", ec.message());
                    return false;
                }
            }

            if (entry.is_directory())
                entries.dirs.push_back(entry.path());
            else if (entry.is_regular_file())
                entries.files.push_back(entry.path());
        }

        // Delete files first
        if (!clean_files(entries.files, ctx))
            return false;

        // Delete directories bottom-up
        if (!clean_directories(base, entries.dirs, ctx))
            return false;

        return true;
    }

    namespace
    {
        /// Compare a file on disk with a file in the archive
        /// @return true if contents match exactly, false otherwise
        bool compare_file_contents(const std::filesystem::path& disk_path,
                                   const std::string& archive_path,
                                   SnapshotReader* reader)
        {
            // Read archive file
            auto archive_data = reader->read_binary(archive_path);
            if (archive_data.empty())
            {
                // Could be empty file - check if it exists
                if (!reader->exists(archive_path))
                    return false;
            }

            // Read disk file
            std::error_code ec;
            auto file_size = std::filesystem::file_size(disk_path, ec);
            if (ec)
                return false;

            // Quick size check
            if (file_size != archive_data.size())
                return false;

            // Read and compare
            std::ifstream file(disk_path, std::ios::binary);
            if (!file)
                return false;

            std::vector<uint8_t> disk_data(static_cast<size_t>(file_size));
            if (file_size > 0)
            {
                file.read(reinterpret_cast<char*>(disk_data.data()), static_cast<std::streamsize>(file_size));
                if (!file)
                    return false;
            }

            return disk_data == archive_data;
        }
    } // anonymous namespace

    VerifyResult CopyDirectoryAction::verify(ActionContext *ctx) const
    {
        // Resolve path variables
        const std::string resolved_path = ctx->blueprint()->resolve(m_path);
        VerifyResult result;

        // Check if directory exists on system
        bool exists_on_system = pnq::directory::exists(resolved_path);

        // If no reader available, just check top-level existence (project verification)
        if (!ctx->reader())
        {
            if (!exists_on_system)
            {
                result.status = VerifyResult::Status::Missing;
                result.detail = "Directory not found";
            }
            else
            {
                result.status = VerifyResult::Status::Match;
                result.detail = "Directory exists";
            }
            return result;
        }

        // Reader available - do file-level comparison (instance verification)
        auto* reader = ctx->reader();

        // Check if exists in snapshot
        bool exists_in_snapshot = reader->exists(m_archive_path);

        if (!exists_on_system && !exists_in_snapshot)
        {
            result.status = VerifyResult::Status::Missing;
            result.detail = "Directory not found on system or in snapshot";
            return result;
        }

        if (!exists_on_system && exists_in_snapshot)
        {
            result.status = VerifyResult::Status::Missing;
            result.detail = "Directory exists in snapshot but not on system";
            return result;
        }

        // Get archive entries under this prefix
        auto archive_entries = collect_archive_entries(m_archive_path, ctx);

        // Build set of archive files (normalized to forward slashes for comparison)
        std::unordered_set<std::string> archive_file_set;
        for (const auto& rel : archive_entries.files)
        {
            archive_file_set.insert(rel);
        }

        // Collect filesystem files
        std::filesystem::path base{resolved_path};
        std::error_code ec;
        std::unordered_set<std::string> fs_file_set;
        std::unordered_map<std::string, std::filesystem::path> fs_file_paths;

        if (exists_on_system)
        {
            auto iterator = m_recursive
                ? std::filesystem::recursive_directory_iterator(base, ec)
                : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::none, ec);

            if (!ec)
            {
                for (const auto& entry : iterator)
                {
                    if (entry.is_regular_file())
                    {
                        // Skip blueprint.xml
                        std::string filename = entry.path().filename().string();
                        if (pnq::string::equals_nocase(filename, "blueprint.xml"))
                            continue;

                        // Apply filters
                        if (!matches_filters(filename))
                            continue;

                        // Compute relative path with forward slashes
                        auto rel = std::filesystem::relative(entry.path(), base, ec);
                        if (ec)
                            continue;

                        std::string rel_str = rel.string();
                        std::replace(rel_str.begin(), rel_str.end(), '\\', '/');

                        fs_file_set.insert(rel_str);
                        fs_file_paths[rel_str] = entry.path();
                    }
                }
            }
        }

        // Normalize archive prefix
        std::string prefix = m_archive_path;
        if (!prefix.empty() && prefix.back() == '/')
            prefix.pop_back();

        // Compare: iterate through archive files
        for (const auto& rel_file : archive_entries.files)
        {
            auto fs_it = fs_file_set.find(rel_file);
            if (fs_it == fs_file_set.end())
            {
                // In snapshot but not on filesystem
                result.missing_files.push_back(rel_file);
                result.file_missing_count++;
            }
            else
            {
                // Both exist - compare contents
                std::string archive_full_path = prefix + "/" + rel_file;
                const auto& disk_path = fs_file_paths[rel_file];

                if (compare_file_contents(disk_path, archive_full_path, reader))
                {
                    result.file_match_count++;
                }
                else
                {
                    result.mismatched_files.push_back(rel_file);
                    result.file_mismatch_count++;
                }

                // Remove from fs_file_set so we know what's left (extras)
                fs_file_set.erase(fs_it);
            }
        }

        // Remaining files in fs_file_set are on filesystem but not in snapshot
        for (const auto& rel_file : fs_file_set)
        {
            result.extra_files.push_back(rel_file);
            result.file_extra_count++;
        }

        // Determine overall status
        int total_archive = static_cast<int>(archive_entries.files.size());

        if (result.file_match_count == total_archive && result.file_extra_count == 0)
        {
            result.status = VerifyResult::Status::Match;
            result.detail = std::format("{} files verified", result.file_match_count);
        }
        else if (result.file_match_count == 0 && total_archive > 0)
        {
            result.status = VerifyResult::Status::Missing;
            result.detail = std::format("{} files missing", result.file_missing_count);
        }
        else
        {
            result.status = VerifyResult::Status::Mismatch;
            std::vector<std::string> parts;
            if (result.file_match_count > 0)
                parts.push_back(std::format("{} match", result.file_match_count));
            if (result.file_mismatch_count > 0)
                parts.push_back(std::format("{} differ", result.file_mismatch_count));
            if (result.file_missing_count > 0)
                parts.push_back(std::format("{} missing", result.file_missing_count));
            if (result.file_extra_count > 0)
                parts.push_back(std::format("{} extra", result.file_extra_count));
            result.detail = pnq::string::join(parts, ", ");
        }

        return result;
    }

    std::vector<std::pair<std::string, std::string>> CopyDirectoryAction::to_params() const
    {
        std::vector<std::pair<std::string, std::string>> params = {
            {"path", m_path},
            {"archive", m_archive_path}};

        if (!m_recursive)
            params.emplace_back("recursive", "false");

        for (const auto &filter : m_include_filters)
            params.emplace_back("include", filter);

        for (const auto &filter : m_exclude_filters)
            params.emplace_back("exclude", filter);

        return params;
    }

    std::string CopyDirectoryAction::describe_clean() const
    {
        return "Folder: " + m_path;
    }

} // namespace insti
