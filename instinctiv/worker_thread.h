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

namespace instinctiv
{

// =============================================================================
// Messages from UI to Worker
// =============================================================================

struct StartBackup
{
    const insti::Blueprint* blueprint;
    std::string output_path;
};

struct StartRestore
{
    std::string archive_path;
    std::unordered_map<std::string, std::string> variable_overrides;
};

struct StartClean
{
    const insti::Blueprint* blueprint;
    std::string project;  // Project name for installation registry update
    bool simulate = false;
};

struct StartVerify
{
    const insti::Blueprint* blueprint;
};

struct RefreshRegistry
{
    std::vector<std::string> roots;
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
    enum class Level { Info, Warning, Error };
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
    std::string snapshot_path;  // Path to snapshot (for backup/restore)
};

struct VerifyComplete
{
    std::vector<insti::VerifyResult> results;
};

struct RegistryRefreshComplete
{
    bool success;
    pnq::RefCountedVector<insti::ProjectBlueprint*> project_blueprints;   // Ownership transferred to UI
    pnq::RefCountedVector<insti::InstanceBlueprint*> instance_blueprints; // Ownership transferred to UI
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
    void do_refresh_registry(const RefreshRegistry& cmd);

    /// Post a message to the UI (called from worker thread).
    void post_to_ui(UIMessage msg);

    /// Wait for a decision response from UI (blocks worker thread).
    insti::IActionCallback::Decision wait_for_decision();

    rigtorp::SPSCQueue<WorkerMessage> m_to_worker{QUEUE_SIZE};
    rigtorp::SPSCQueue<UIMessage> m_to_ui{QUEUE_SIZE};

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_cancel_requested{false};
    std::atomic<bool> m_waiting_for_decision{false};
};

} // namespace instinctiv
