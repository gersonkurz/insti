#pragma once

// =============================================================================
// worker_thread.h - Background worker for Orchestrator operations
// =============================================================================

#include <insti/insti.h>
#include <rigtorp/SPSCQueue.h>

#include <thread>
#include <variant>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

namespace pnq
{
	template <typename T> class RefCountedInstance
	{
		T m_value;
	public:
		explicit RefCountedInstance(T value)
			: m_value{ value }
		{
			PNQ_ADDREF(m_value);
		}
		RefCountedInstance()
			: m_value{ nullptr }
		{
		}
		~RefCountedInstance()
		{
			PNQ_RELEASE(m_value);
		}
		// Non-copyable
		RefCountedInstance(const RefCountedInstance& other)
			: m_value{ other.m_value }
		{
			PNQ_ADDREF(m_value);
		}
		RefCountedInstance& operator=(const RefCountedInstance& other)
		{
			if (this != &other)
			{
				PNQ_RELEASE(m_value);
				m_value = other.m_value;
				PNQ_ADDREF(m_value);
			}
			return *this;
		}

		RefCountedInstance(RefCountedInstance&& other) noexcept
			: m_value{ other.m_value }
		{
			other.m_value = T{};
		}

		RefCountedInstance& operator=(RefCountedInstance&& other) noexcept
		{
			if (this != &other)
			{
				PNQ_RELEASE(m_value);
				m_value = other.m_value;
				other.m_value = T{};
			}
			return *this;
		}
		T get() const { return m_value; }
		T get() { return m_value; }
	};
}

namespace instinctiv
{

	// =============================================================================
	// Messages from UI to Worker
	// =============================================================================

	class StartBackup final
	{
	public:
		StartBackup(insti::SnapshotRegistry* snapshot_registry, insti::Project* project, std::string_view path, std::string_view description = "")
			: m_snapshot_registry{ snapshot_registry }
			, m_project{ project }
			, m_output_path{ path }
			, m_description{ description }
		{
		}

		pnq::RefCountedInstance<insti::Project*> m_project;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
		std::string m_output_path;
		std::string m_description;
	};

	class StartRestore final
	{
	public:
		StartRestore(insti::SnapshotRegistry* snapshot_registry, std::string_view path, const std::unordered_map<std::string, std::string>& vo)
			: m_snapshot_registry{ snapshot_registry }
			, m_variable_overrides{ vo }
			, m_archive_path{ path }
		{
		}
		std::string m_archive_path;
		std::unordered_map<std::string, std::string> m_variable_overrides;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
	};

	class StartClean final
	{
	public:
		StartClean(insti::SnapshotRegistry* snapshot_registry, insti::Blueprint* bp, bool simulate = false)
			: m_snapshot_registry{ snapshot_registry }
			, m_blueprint{ bp}
			, m_simulate{ simulate }
		{
		}
		pnq::RefCountedInstance<insti::Blueprint*> m_blueprint;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
		bool m_simulate;
	};

	class StartVerify final
	{
	public:
		/// Project verification (existence check only)
		StartVerify(insti::SnapshotRegistry* snapshot_registry, insti::Blueprint* bp)
			: m_snapshot_registry{ snapshot_registry }
			, m_blueprint{ bp }
		{
		}

		/// Instance verification (file-level comparison)
		StartVerify(insti::SnapshotRegistry* snapshot_registry, insti::Blueprint* bp, std::string_view archive_path)
			: m_snapshot_registry{ snapshot_registry }
			, m_blueprint{ bp }
			, m_archive_path{ archive_path }
		{
		}

		pnq::RefCountedInstance<insti::Blueprint*> m_blueprint;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
		std::string m_archive_path;  // Empty for project verification, set for instance verification
	};

	struct RefreshRegistry
	{
		std::vector<std::string> roots;
	};

	class StartHook final
	{
	public:
		StartHook(insti::Blueprint* bp, insti::IHook* hook)
			: m_blueprint{ bp }
			, m_hook{ hook }
		{
		}
		pnq::RefCountedInstance<insti::Blueprint*> m_blueprint;
		pnq::RefCountedInstance<insti::IHook*> m_hook;
	};

	class StartStartup final
	{
	public:
		StartStartup(insti::SnapshotRegistry* snapshot_registry, insti::Blueprint* bp)
			: m_snapshot_registry{ snapshot_registry }
			, m_blueprint{ bp }
		{
		}
		pnq::RefCountedInstance<insti::Blueprint*> m_blueprint;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
	};

	class StartShutdown final
	{
	public:
		StartShutdown(insti::SnapshotRegistry* snapshot_registry, insti::Blueprint* bp)
			: m_snapshot_registry{ snapshot_registry }
			, m_blueprint{ bp }
		{
		}
		pnq::RefCountedInstance<insti::Blueprint*> m_blueprint;
		pnq::RefCountedInstance<insti::SnapshotRegistry*> m_snapshot_registry;
	};

	struct DecisionResponse
	{
		insti::IActionCallback::Decision decision;
	};

	struct CancelOperation
	{
		// No parameters - signals worker to abort current operation
	};

	struct ShutdownWorker
	{
		// No parameters - signals worker thread to exit
	};

	using WorkerMessage = std::variant<
		StartBackup,
		StartRestore,
		StartClean,
		StartVerify,
		StartHook,
		StartStartup,
		StartShutdown,
		RefreshRegistry,
		DecisionResponse,
		CancelOperation,
		ShutdownWorker
	>;

	// =============================================================================
	// Messages from Worker to UI
	// =============================================================================

	struct Progress
	{
		std::string phase;
		std::string detail;
		int percent; // -1 for indeterminate
	};

	struct LogEntry
	{
		enum class Level { Info, Warning, Error, Success };
		Level level;
		std::string message;
	};

	struct ErrorDecision
	{
		std::string message;
		std::string context;
	};

	struct FileConflict
	{
		std::string path;
		std::string action;
	};

	struct OperationComplete
	{
		bool success;
		std::string message;
		std::string project;        // Project name (for backup/restore)
	};

	struct VerifyComplete
	{
		std::vector<insti::VerifyResult> results;
	};

	struct RegistryRefreshComplete
	{
		bool success;
		insti::SnapshotRegistry* snapshot_registry;
	};

	using UIMessage = std::variant<
		Progress,
		LogEntry,
		ErrorDecision,
		FileConflict,
		OperationComplete,
		VerifyComplete,
		RegistryRefreshComplete
	>;

	// =============================================================================
	// WorkerCallback - IActionCallback that posts to UI queue
	// =============================================================================

	class WorkerThread; // Forward declaration

	class WorkerCallback : public insti::IActionCallback
	{
	public:
		explicit WorkerCallback(WorkerThread* worker);

		void on_progress(std::string_view phase, std::string_view detail, int percent) override;
		void on_warning(std::string_view message) override;
		Decision on_error(std::string_view message, std::string_view context) override;
		Decision on_file_conflict(std::string_view path, std::string_view action) override;

	private:
		Decision wait_for_decision();

		WorkerThread* m_worker;
	};

	// =============================================================================
	// WorkerThread - Owns thread and message queues
	// =============================================================================

	class WorkerThread
	{
	public:
		static constexpr size_t QUEUE_SIZE = 256;

		WorkerThread();
		~WorkerThread();

		// Non-copyable, non-movable
		WorkerThread(const WorkerThread&) = delete;
		WorkerThread& operator=(const WorkerThread&) = delete;

		/// Post a message to the worker (non-blocking).
		void post(WorkerMessage msg);

		/// Poll for a message from the worker (non-blocking).
		/// Returns std::nullopt if no message available.
		std::optional<UIMessage> poll();

		/// Check if the worker is currently busy with an operation.
		bool is_busy() const { return m_busy.load(); }

		/// Request cancellation of current operation.
		void cancel() { m_cancel_requested.store(true); }

		/// Check if cancellation was requested.
		bool is_cancel_requested() const { return m_cancel_requested.load(); }

		/// Clear cancellation flag.
		void clear_cancel() { m_cancel_requested.store(false); }

	private:
		friend class WorkerCallback;

		void thread_func();
		void process_message(const WorkerMessage& msg);

		void do_backup(const StartBackup& cmd);
		void do_restore(const StartRestore& cmd);
		void do_clean(const StartClean& cmd);
		void do_verify(const StartVerify& cmd);
		void do_hook(const StartHook& cmd);
		void do_startup(const StartStartup& cmd);
		void do_shutdown(const StartShutdown& cmd);
		void do_refresh_registry(const RefreshRegistry& cmd);

		/// Post a message to the UI (called from worker thread).
		void post_to_ui(UIMessage msg);

		/// Wait for a decision response from UI (blocks worker thread).
		insti::IActionCallback::Decision wait_for_decision();

		rigtorp::SPSCQueue<WorkerMessage> m_to_worker{ QUEUE_SIZE };
		rigtorp::SPSCQueue<UIMessage> m_to_ui{ QUEUE_SIZE };

		std::thread m_thread;
		std::atomic<bool> m_running{ false };
		std::atomic<bool> m_busy{ false };
		std::atomic<bool> m_cancel_requested{ false };
		std::atomic<bool> m_waiting_for_decision{ false };
	};

} // namespace instinctiv
