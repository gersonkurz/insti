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
	class Instance;
	class Project;
	class SnapshotRegistry;

	/// Coordinates backup/restore/clean operations with hooks.
	class Orchestrator final
	{
		SnapshotRegistry* m_snapshot_registry;
	public:
		Orchestrator(SnapshotRegistry* snapshot_registry);
		~Orchestrator();
		PNQ_DECLARE_NON_COPYABLE(Orchestrator);

		/// Backup blueprint to snapshot.
		/// Runs: shutdown -> backup -> startup
		/// @param bp Blueprint (must not be nullptr)
		/// @param output_path Output snapshot file path
		/// @param cb Callback for progress/errors (may be nullptr for silent operation)
		/// @param force If true, also run force-only shutdown hooks (aggressive termination)
		/// @param description Optional description for this snapshot (overrides project description)
		/// @return true on success
		bool backup(const Project* bp, std::string_view output_path, IActionCallback* cb, bool force = false, const std::string& description = {});

		/// Restore from snapshot with pre-loaded blueprint (allows variable overrides via context).
		/// Runs: restore -> startup
		/// @param bp Blueprint (must not be nullptr)
		/// @param archive_path Path to snapshot file
		/// @param cb Callback for progress/errors (may be nullptr for silent operation)
		/// @param simulate If true, log actions without performing them
		/// @param force If true, also run force-only startup hooks
		/// @return true on success
		bool restore(const Instance* bp, std::string_view archive_path, IActionCallback* cb, bool simulate = false, bool force = false);

		/// Clean resources defined in blueprint.
		/// Runs: shutdown -> clean
		/// @param bp Blueprint (must not be nullptr)
		/// @param cb Callback for progress/errors (may be nullptr for silent operation)
		/// @param simulate If true, log actions without performing them
		/// @param force If true, also run force-only shutdown hooks (aggressive termination)
		/// @return true on success
		bool clean(const Blueprint* bp, IActionCallback* cb, bool simulate = false, bool force = false);

		/// Verify blueprint against live system.
		/// @param bp Blueprint (must not be nullptr)
		/// @param cb Callback for progress (may be nullptr)
		/// @return Verification results for each action
		std::vector<VerifyResult> verify(const Blueprint* bp, IActionCallback* cb);
	};

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
