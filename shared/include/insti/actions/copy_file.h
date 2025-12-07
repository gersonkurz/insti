#pragma once

#include <insti/actions/action.h>
#include <string>
#include <pnq/pnq.h>

namespace insti
{

    /// Captures and restores a single file.
    ///
    /// Use this for isolated files like DLLs in System32 or shared config files
    /// that don't belong to a directory tree.
    class CopyFileAction : public IAction
    {
        PNQ_DECLARE_NON_COPYABLE(CopyFileAction)

    public:
        static constexpr std::string_view TYPE_NAME = "file";

        /// @param path Filesystem path (may contain variables like ${WINDIR})
        /// @param archive_path Path within the snapshot archive
        /// @param description Optional user-facing description (defaults to "File: {path}")
        CopyFileAction(std::string path, std::string archive_path, std::string description = {});

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

        const std::string m_path;
        const std::string m_archive_path;
    };

} // namespace insti
