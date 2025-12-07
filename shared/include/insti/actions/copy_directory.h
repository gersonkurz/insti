#pragma once

#include <insti/actions/action.h>
#include <string>
#include <vector>
#include <pnq/pnq.h>

namespace insti
{

    /// Captures and restores a directory tree.
    ///
    /// On backup, recursively copies all files from the source path into the snapshot.
    /// On restore, extracts the files to the resolved destination path.
    /// On clean, removes the entire directory.
    ///
    /// Supports optional include/exclude glob filters (e.g., "*.dll", "*.log").
    class CopyDirectoryAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(CopyDirectoryAction)

    public:
        static constexpr std::string_view TYPE_NAME = "files";

        /// @param path Filesystem path (may contain variables like ${PROGRAMFILES})
        /// @param archive_path Relative path prefix within the snapshot archive
        /// @param description Optional user-facing description (defaults to "Files: {path}")
        /// @param recursive Whether to recurse into subdirectories (default: true)
        /// @param include_filters Glob patterns to include (empty = include all)
        /// @param exclude_filters Glob patterns to exclude (applied after include)
        CopyDirectoryAction(std::string path, std::string archive_path, std::string description = {},
                            bool recursive = true,
                            std::vector<std::string> include_filters = {},
                            std::vector<std::string> exclude_filters = {})
            : IAction{std::string{TYPE_NAME}, description.empty() ? "Files: " + path : std::move(description)}
            , m_path{std::move(path)}, m_archive_path{std::move(archive_path)}
            , m_recursive{recursive}, m_include_filters{std::move(include_filters)}
            , m_exclude_filters{std::move(exclude_filters)}
        {
        }

        /// @name Accessors for inspection and testing
        /// @{
        const std::string &path() const { return m_path; }
        const std::string &archive_path() const { return m_archive_path; }
        /// @}

    private:
        std::vector<std::pair<std::string, std::string>> to_params() const override;
        bool backup(ActionContext *ctx) const override;
        bool restore(ActionContext *ctx) const override;
        bool do_clean(ActionContext *ctx) const override;
        VerifyResult verify(ActionContext *ctx) const override;

        /// Check if a filename matches the include/exclude filters.
        /// @param filename Filename to check (not full path)
        /// @return true if file should be included, false if filtered out
        bool matches_filters(std::string_view filename) const;

        /// Collected directory entries for backup.
        struct CollectedEntries
        {
            std::vector<std::filesystem::path> dirs;
            std::vector<std::filesystem::path> files;
        };

        /// Collect directories and files from source, applying filters.
        /// @return collected entries, or nullopt on abort
        std::optional<CollectedEntries> collect_entries(
            const std::filesystem::path &base, ActionContext *ctx) const;

        /// Create empty directories (those without files) in archive.
        /// @return true to continue, false on abort
        bool backup_empty_directories(
            const std::filesystem::path &base, const CollectedEntries &entries,
            std::string_view archive_prefix, ActionContext *ctx) const;

        /// Backup files to archive with progress reporting.
        /// @return true to continue, false on abort
        bool backup_files(
            const std::filesystem::path &base, const std::vector<std::filesystem::path> &files,
            std::string_view archive_prefix, ActionContext *ctx) const;

        /// Delete files with retry/SkipAll support and progress reporting.
        /// @return true to continue, false on abort
        bool clean_files(
            const std::vector<std::filesystem::path> &files, ActionContext *ctx) const;

        /// Delete directories bottom-up (deepest first) with retry/SkipAll support.
        /// @return true to continue, false on abort
        bool clean_directories(
            const std::filesystem::path &base,
            const std::vector<std::filesystem::path> &dirs, ActionContext *ctx) const;

        /// Collected archive entries for restore.
        struct ArchiveEntries
        {
            std::vector<std::string> dirs;   ///< Directory paths (relative to archive_prefix)
            std::vector<std::string> files;  ///< File paths (relative to archive_prefix)
        };

        /// Collect directories and files from archive under prefix.
        ArchiveEntries collect_archive_entries(std::string_view archive_prefix, ActionContext *ctx) const;

        /// Create directories for restore (top-down).
        /// @return true to continue, false on abort
        bool restore_directories(
            const std::filesystem::path &dest_base,
            const std::vector<std::string> &rel_dirs, ActionContext *ctx) const;

        /// Extract files from archive with retry/SkipAll/progress.
        /// @return true to continue, false on abort
        bool restore_files(
            std::string_view archive_prefix, const std::filesystem::path &dest_base,
            const std::vector<std::string> &rel_files, ActionContext *ctx) const;

        const std::string m_path;
        const std::string m_archive_path;
        const bool m_recursive;
        const std::vector<std::string> m_include_filters;
        const std::vector<std::string> m_exclude_filters;
    };

} // namespace insti
