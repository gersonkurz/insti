#pragma once

// =============================================================================
// insti/core/action_callback.h - Callback interface for action feedback
// =============================================================================

#include <pnq/ref_counted.h>
#include <string_view>

namespace insti
{

    /// Callback interface for action progress, warnings, and error handling.
    /// Implementations can be CLI (auto-abort) or GUI (show dialogs).
    /// Refcounted - use PNQ_ADDREF/PNQ_RELEASE for ownership.
    class IActionCallback : public pnq::RefCountImpl
    {
    public:
        /// Decision returned by error handlers.
        enum class Decision
        {
            Continue, ///< Proceed to next item
            Retry,    ///< Retry the failed operation
            Skip,     ///< Skip this item, ask again on next error
            SkipAll,  ///< Skip this and all future errors (don't ask again)
            Abort     ///< Stop entire operation
        };

        /// Progress reporting.
        /// @param phase Current operation phase (e.g., "Backup", "Restore")
        /// @param detail Specific item being processed
        /// @param percent Progress percentage (0-100), or -1 for indeterminate
        virtual void on_progress(std::string_view phase, std::string_view detail, int percent) = 0;

        /// Warning notification (execution continues).
        virtual void on_warning(std::string_view message) = 0;

        /// Error with decision request.
        /// @param message Error description
        /// @param context Additional context (e.g., file path, operation)
        /// @return Decision on how to proceed
        virtual Decision on_error(std::string_view message, std::string_view context) = 0;

        /// File conflict during restore (file already exists).
        /// @param path Path to the conflicting file
        /// @param action Description of intended action
        /// @return Decision on how to proceed
        virtual Decision on_file_conflict(std::string_view path, std::string_view action) = 0;
    };

} // namespace insti
