#pragma once

#include <insti/core/action_callback.h>
#include <pnq/ref_counted.h>
#include <string>
#include <utility>
#include <vector>

namespace insti
{

    class ActionContext;

    /// Result of verify operation
    struct VerifyResult
    {
        enum class Status
        {
            Match,    // Resource matches expectation
            Mismatch, // Resource exists but differs
            Missing,  // Resource expected but not found
            Extra     // Resource found but not expected
        };

        Status status = Status::Missing;
        std::string detail; // Human-readable explanation
    };

    /// Abstract base class for all actions (ref-counted)
    class IAction : public pnq::RefCountImpl
    {
    public:
        /// Returns the action type name (e.g., "files", "registry", "service")
        const std::string& type_name() const { return m_type_name; }

        /// User-facing description for progress reporting
        const std::string& description() const { return m_description; }

        /// Backup the resource to the snapshot
        /// @param ctx Action context with writer, blueprint, callback
        /// @return true on success
        virtual bool backup(ActionContext *ctx) const = 0;

        /// Restore the resource from the snapshot
        /// @param ctx Action context with reader, blueprint, callback
        /// @return true on success
        virtual bool restore(ActionContext *ctx) const = 0;

        /// Clean/remove the resource from the system.
        /// Default implementation calls do_clean() and handles errors.
        /// Override for complex multi-step clean operations.
        /// @param ctx Action context with blueprint, callback
        /// @return true on success
        virtual bool clean(ActionContext *ctx) const;

        /// Verify the resource against expected state
        /// @param ctx Action context
        /// @return Verification result with status and detail
        virtual VerifyResult verify(ActionContext *ctx) const = 0;

        /// Format-agnostic serialization for roundtrip
        /// @return Key-value pairs that can be serialized to any format
        virtual std::vector<std::pair<std::string, std::string>> to_params() const = 0;

    protected:
        IAction(std::string type_name, std::string description)
            : m_type_name{std::move(type_name)}
            , m_description{std::move(description)}
        {}

        /// Handle callback decision for single-shot operations.
        /// Updates ctx->skip_all_errors if SkipAll is chosen.
        /// @return true to continue, false to abort
        static bool handle_decision(IActionCallback::Decision decision, ActionContext *ctx);

        /// Check if archive path exists in snapshot, report error if not.
        /// @return true if exists or user chose to continue, false to abort
        static bool check_archive_exists(const std::string &archive_path, ActionContext *ctx);

        /// Override to implement clean logic. Called by default clean() implementation.
        /// @return true on success, false on failure
        virtual bool do_clean(ActionContext *ctx) const = 0;

    private:
        const std::string m_type_name;
        const std::string m_description;
    };

} // namespace insti
