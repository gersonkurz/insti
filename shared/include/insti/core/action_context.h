#pragma once

// =============================================================================
// insti/core/action_context.h - Context for action execution
// =============================================================================

#include <pnq/ref_counted.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace insti
{

    class Blueprint;
    class SnapshotReader;
    class SnapshotWriter;
    class IActionCallback;

    /// Context passed to actions during backup/restore/clean operations.
    ///
    /// Holds references to blueprint, reader/writer, and callback.
    /// Also manages runtime variable overrides that are applied on top of
    /// blueprint variables without modifying the blueprint itself.
    ///
    /// Refcounted - use PNQ_ADDREF/PNQ_RELEASE for ownership management.
    /// Not copyable.
    class ActionContext : public pnq::RefCountImpl
    {
    public:
        /// @name Factory Methods
        /// @{

        /// Create context for backup operation.
        /// @param blueprint Blueprint (retained, must not be nullptr)
        /// @param writer Snapshot writer for output (retained)
        /// @param callback Callback for progress/errors (retained, may be nullptr)
        static ActionContext *for_backup(const Blueprint *blueprint, SnapshotWriter *writer, IActionCallback *callback);

        /// Create context for restore operation.
        /// @param blueprint Blueprint (retained, must not be nullptr)
        /// @param reader Snapshot reader for input (retained)
        /// @param callback Callback for progress/errors (retained, may be nullptr)
        static ActionContext *for_restore(const Blueprint *blueprint, SnapshotReader *reader, IActionCallback *callback);

        /// Create context for clean operation.
        /// @param blueprint Blueprint (retained, must not be nullptr)
        /// @param callback Callback for progress/errors (retained, may be nullptr)
        static ActionContext *for_clean(const Blueprint *blueprint, IActionCallback *callback);

        /// @}

        /// @name Accessors
        /// @{

        const Blueprint *blueprint() const { return m_blueprint; }
        SnapshotReader *reader() const { return m_reader; }
        SnapshotWriter *writer() const { return m_writer; }
        IActionCallback *callback() const { return m_callback; }

        /// @}

        /// @name Simulation Mode
        /// @{

        /// Check if simulate (dry-run) mode is active.
        /// In simulate mode, actions log what they would do without actually performing operations.
        bool simulate() const { return m_simulate; }

        /// Enable/disable simulate mode.
        void set_simulate(bool value) { m_simulate = value; }

        /// @}

        /// @name Error Handling State
        /// @{

        /// Check if SkipAll mode is active (skip errors without prompting).
        bool skip_all_errors() const { return m_skip_all_errors; }

        /// Enable SkipAll mode (typically called when user chooses SkipAll).
        void set_skip_all_errors(bool value) { m_skip_all_errors = value; }

        /// @}

        /// @name Variable Resolution
        /// @{

        /// Set a runtime variable override.
        /// Overrides are applied on top of blueprint variables when calling variables().
        /// @param name Variable name
        /// @param value Override value
        void set_override(std::string_view name, std::string_view value);

        /// Get effective variables (blueprint variables merged with overrides).
        /// Overrides take precedence over blueprint variables.
        const std::unordered_map<std::string, std::string> &variables() const;

        /// @}

        // Not copyable
        ActionContext(const ActionContext &) = delete;
        ActionContext &operator=(const ActionContext &) = delete;

    protected:
        ~ActionContext();

    private:
        ActionContext(const Blueprint* blueprint, SnapshotReader* reader, SnapshotWriter* writer, IActionCallback* callback);

        /// Rebuild merged variables from blueprint + overrides.
        void rebuild_merged_variables() const;

        const Blueprint *m_blueprint = nullptr;
        SnapshotReader *m_reader = nullptr;
        SnapshotWriter *m_writer = nullptr;
        IActionCallback *m_callback = nullptr;
        bool m_simulate = false;
        bool m_skip_all_errors = false;

        std::unordered_map<std::string, std::string> m_overrides;
        mutable std::unordered_map<std::string, std::string> m_merged_variables;
        mutable bool m_merged_dirty = true;
    };

} // namespace insti
