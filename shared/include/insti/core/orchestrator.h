#pragma once

// =============================================================================
// insti/core/orchestrator.h - Coordinates backup/restore/clean operations
// =============================================================================

#include <insti/core/action_callback.h>
#include <insti/actions/action.h>
#include <pnq/pnq.h>
#include <string_view>
#include <vector>

namespace insti
{

    class Blueprint;

    /// Coordinates backup/restore/clean operations with hooks.
    namespace orchestrator
    {
        /// Backup blueprint to snapshot.
        /// @param bp Blueprint (must not be nullptr)
        /// @param output_path Output snapshot file path
        /// @param cb Callback for progress/errors (may be nullptr for silent operation)
        /// @return true on success
        bool backup(const Blueprint *bp, std::string_view output_path, IActionCallback *cb);

        /// Restore from snapshot.
        /// @param archive_path Path to snapshot file
        /// @param cb Callback for progress/errors (may be nullptr for silent operation)
        /// @param simulate If true, log actions without performing them
        /// @return true on success
        bool restore(std::string_view archive_path, IActionCallback *cb, bool simulate = false);

        /// Restore from snapshot with pre-loaded blueprint (allows variable overrides via context).
        /// @param bp Blueprint (must not be nullptr)
        /// @param archive_path Path to snapshot file
        /// @param cb Callback for progress/errors (may be nullptr for silent operation)
        /// @param simulate If true, log actions without performing them
        /// @return true on success
        bool restore(const Blueprint *bp, std::string_view archive_path, IActionCallback *cb, bool simulate = false);

        /// Clean resources defined in blueprint.
        /// @param bp Blueprint (must not be nullptr)
        /// @param cb Callback for progress/errors (may be nullptr for silent operation)
        /// @param simulate If true, log actions without performing them
        /// @return true on success
        bool clean(const Blueprint *bp, IActionCallback *cb, bool simulate = false);

        /// Verify blueprint against live system.
        /// @param bp Blueprint (must not be nullptr)
        /// @param cb Callback for progress (may be nullptr)
        /// @return Verification results for each action
        std::vector<VerifyResult> verify(const Blueprint *bp, IActionCallback *cb);
    } // namespace orchestrator

    /// Simple callback that aborts on first error.
    /// Suitable for CLI usage where interactive decisions aren't needed.
    class AbortOnErrorCallback : public IActionCallback
    {
        PNQ_DECLARE_NON_COPYABLE(AbortOnErrorCallback)
    public:
        AbortOnErrorCallback() = default;
        void on_progress(std::string_view phase, std::string_view detail, int percent) override;
        void on_warning(std::string_view message) override;
        Decision on_error(std::string_view message, std::string_view context) override;
        Decision on_file_conflict(std::string_view path, std::string_view action) override;
    };

    /// Null callback that ignores all events.
    class NullCallback : public IActionCallback
    {
        PNQ_DECLARE_NON_COPYABLE(NullCallback)
    public:
        NullCallback() = default;
        void on_progress(std::string_view, std::string_view, int) override {}
        void on_warning(std::string_view) override {}
        Decision on_error(std::string_view, std::string_view) override { return Decision::Abort; }
        Decision on_file_conflict(std::string_view, std::string_view) override { return Decision::Continue; }
    };

} // namespace insti
