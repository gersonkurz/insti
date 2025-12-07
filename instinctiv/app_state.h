#pragma once

// =============================================================================
// app_state.h - Application state for instinctiv
// =============================================================================

#include <insti/insti.h>
#include <insti/registry/snapshot_registry.h>
#include "worker_thread.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace instinctiv
{

	/// Installation status for display.
	enum class InstallStatus
	{
		NotInstalled,       // No version installed
		Installed,          // This exact version installed
		DifferentVersion    // Different version installed
	};

	/// Application state singleton.
	class AppState
	{
	public:
		static AppState& instance();

		/// Initialize the application state.
		void initialize();

		/// Shutdown and cleanup.
		void shutdown();

		// UI State - Snapshots
		insti::ProjectBlueprint* selected_snapshot = nullptr;  // Currently selected snapshot

		// UI State - Blueprints
		std::string filter_text;                    // Filter input
		bool filter_dirty = false;                  // Filter needs reapplication
		std::string status_message;                 // Status bar message
		bool is_refreshing = false;                 // Registry refresh in progress

		// First-run state
		bool show_first_run_dialog = false;         // Show setup dialog when registry is empty
		bool first_refresh_done = false;            // True after first registry refresh completes

		// Detail panel state
		std::string detail_error;                   // Error loading blueprint

		// Progress dialog state
		bool show_progress_dialog = false;          // Show progress modal
		std::string progress_operation;             // Current operation name (Backup, Restore, etc.)
		std::string progress_phase;                 // Current phase
		std::string progress_detail;                // Current item being processed
		int progress_percent = -1;                  // Progress percentage (-1 = indeterminate)
		std::vector<std::string> progress_log;      // Log messages
		insti::ProjectBlueprint* active_blueprint = nullptr;  // Blueprint being used for operation (owned)

		// Operation options
		bool dry_run = false;                       // Simulate mode - log actions without performing them

		// Worker thread
		std::unique_ptr<WorkerThread> worker;

		// Registry and settings
		std::vector<std::string> registry_roots;

		// TBD: move to a member instead of a pointer
		insti::SnapshotRegistry* m_snapshot_registry{ nullptr };

	private:
		AppState() = default;
		~AppState();

		void clear_entries();
	};

	/// Color definitions for UI (ABGR format for ImGui).
	namespace colors
	{
		constexpr uint32_t INSTALLED = 0xFF64C864;         // Green (100, 200, 100)
		constexpr uint32_t DIFFERENT_VERSION = 0xFF32B4DC; // Yellow/Orange (220, 180, 50)
		constexpr uint32_t NOT_INSTALLED = 0xFF969696;     // Gray (150, 150, 150)
	}


} // namespace instinctiv
