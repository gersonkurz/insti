#include "pch.h"
#include "worker_thread.h"

namespace instinctiv
{

// =============================================================================
// WorkerCallback
// =============================================================================

WorkerCallback::WorkerCallback(WorkerThread* worker)
    : m_worker(worker)
{
}

void WorkerCallback::on_progress(std::string_view phase, std::string_view detail, int percent)
{
    m_worker->post_to_ui(Progress{std::string{phase}, std::string{detail}, percent});
}

void WorkerCallback::on_warning(std::string_view message)
{
    m_worker->post_to_ui(LogEntry{LogEntry::Level::Warning, std::string{message}});
}

WorkerCallback::Decision WorkerCallback::on_error(std::string_view message, std::string_view context)
{
    // Check if cancelled
    if (m_worker->is_cancel_requested())
        return Decision::Abort;

    m_worker->post_to_ui(ErrorDecision{std::string{message}, std::string{context}});
    return m_worker->wait_for_decision();
}

WorkerCallback::Decision WorkerCallback::on_file_conflict(std::string_view path, std::string_view action)
{
    // Check if cancelled
    if (m_worker->is_cancel_requested())
        return Decision::Abort;

    m_worker->post_to_ui(FileConflict{std::string{path}, std::string{action}});
    return m_worker->wait_for_decision();
}

// =============================================================================
// WorkerThread
// =============================================================================

WorkerThread::WorkerThread()
{
    m_running.store(true);
    m_thread = std::thread(&WorkerThread::thread_func, this);
}

WorkerThread::~WorkerThread()
{
    // Signal shutdown
    post(ShutdownWorker{});

    // Wait for thread to exit
    if (m_thread.joinable())
        m_thread.join();
}

void WorkerThread::post(WorkerMessage msg)
{
    while (!m_to_worker.try_push(std::move(msg)))
    {
        // Queue full - spin wait (shouldn't happen with reasonable queue size)
        std::this_thread::yield();
    }
}

std::optional<UIMessage> WorkerThread::poll()
{
    UIMessage* msg = m_to_ui.front();
    if (msg)
    {
        UIMessage result = std::move(*msg);
        m_to_ui.pop();
        return result;
    }
    return std::nullopt;
}

void WorkerThread::post_to_ui(UIMessage msg)
{
    while (!m_to_ui.try_push(std::move(msg)))
    {
        std::this_thread::yield();
    }
}

insti::IActionCallback::Decision WorkerThread::wait_for_decision()
{
    m_waiting_for_decision.store(true);

    while (m_running.load())
    {
        // Check for cancellation
        if (m_cancel_requested.load())
        {
            m_waiting_for_decision.store(false);
            return insti::IActionCallback::Decision::Abort;
        }

        // Check for decision response
        WorkerMessage* msg = m_to_worker.front();
        if (msg)
        {
            if (auto* response = std::get_if<DecisionResponse>(msg))
            {
                auto decision = response->decision;
                m_to_worker.pop();
                m_waiting_for_decision.store(false);
                return decision;
            }
            else if (std::get_if<CancelOperation>(msg))
            {
                m_to_worker.pop();
                m_cancel_requested.store(true);
                m_waiting_for_decision.store(false);
                return insti::IActionCallback::Decision::Abort;
            }
            // Ignore other messages while waiting for decision
            m_to_worker.pop();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    m_waiting_for_decision.store(false);
    return insti::IActionCallback::Decision::Abort;
}

void WorkerThread::thread_func()
{
    while (m_running.load())
    {
        WorkerMessage* msg = m_to_worker.front();
        if (msg)
        {
            WorkerMessage local_msg = std::move(*msg);
            m_to_worker.pop();
            process_message(local_msg);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void WorkerThread::process_message(const WorkerMessage& msg)
{
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, StartBackup>)
            do_backup(arg);
        else if constexpr (std::is_same_v<T, StartRestore>)
            do_restore(arg);
        else if constexpr (std::is_same_v<T, StartClean>)
            do_clean(arg);
        else if constexpr (std::is_same_v<T, StartVerify>)
            do_verify(arg);
        else if constexpr (std::is_same_v<T, RefreshRegistry>)
            do_refresh_registry(arg);
        else if constexpr (std::is_same_v<T, ShutdownWorker>)
            m_running.store(false);
        else if constexpr (std::is_same_v<T, CancelOperation>)
            m_cancel_requested.store(true);
        // DecisionResponse is handled in wait_for_decision()
    }, msg);
}

void WorkerThread::do_backup(const StartBackup& cmd)
{
    m_busy.store(true);
    m_cancel_requested.store(false);

    WorkerCallback callback(this);
    bool success = insti::orchestrator::backup(cmd.blueprint, cmd.output_path, &callback);

    // Extract project name from filename (matches how discover() parses it)
    std::string project;
    std::filesystem::path output_path{cmd.output_path};
    std::string filename = output_path.stem().string(); // Remove .zip
    size_t dash_pos = filename.find('-');
    if (dash_pos != std::string::npos)
        project = filename.substr(0, dash_pos);

    post_to_ui(OperationComplete{
        success,
        success ? "Backup completed" : "Backup failed",
        project,
        cmd.output_path
    });
    m_busy.store(false);
}

void WorkerThread::do_restore(const StartRestore& cmd)
{
    m_busy.store(true);
    m_cancel_requested.store(false);

    WorkerCallback callback(this);

    // Open archive and load blueprint
    insti::ZipSnapshotReader reader;
    if (!reader.open(cmd.archive_path))
    {
        post_to_ui(OperationComplete{false, "Failed to open snapshot", "", ""});
        m_busy.store(false);
        return;
    }

    std::string blueprint_xml = reader.read_text("blueprint.xml");
    if (blueprint_xml.empty())
    {
        post_to_ui(OperationComplete{false, "No blueprint.xml in snapshot", "", ""});
        m_busy.store(false);
        return;
    }

    auto* bp = insti::Blueprint::load_from_string(blueprint_xml);
    if (!bp)
    {
        post_to_ui(OperationComplete{false, "Failed to parse blueprint", "", ""});
        m_busy.store(false);
        return;
    }

    // Apply variable overrides
    for (const auto& [name, value] : cmd.variable_overrides)
        bp->set_override(name, value);

    bool success = insti::orchestrator::restore(bp, cmd.archive_path, &callback);
    bp->release(REFCOUNT_DEBUG_ARGS);

    // Extract project name from filename (matches how discover() parses it)
    std::string project;
    std::filesystem::path archive_path{cmd.archive_path};
    std::string filename = archive_path.stem().string(); // Remove .zip
    size_t dash_pos = filename.find('-');
    if (dash_pos != std::string::npos)
        project = filename.substr(0, dash_pos);

    post_to_ui(OperationComplete{
        success,
        success ? "Restore completed" : "Restore failed",
        project,
        cmd.archive_path
    });
    m_busy.store(false);
}

void WorkerThread::do_clean(const StartClean& cmd)
{
    m_busy.store(true);
    m_cancel_requested.store(false);

    WorkerCallback callback(this);
    bool success = insti::orchestrator::clean(cmd.blueprint, &callback, cmd.simulate);

    std::string msg = cmd.simulate
        ? (success ? "Dry-run completed" : "Dry-run failed")
        : (success ? "Clean completed" : "Clean failed");

    // Pass project name so UI can update installation registry
    // Empty snapshot_path signals this is a clean (not backup/restore)
    post_to_ui(OperationComplete{success, msg, cmd.project, ""});
    m_busy.store(false);
}

void WorkerThread::do_verify(const StartVerify& cmd)
{
    m_busy.store(true);
    m_cancel_requested.store(false);

    WorkerCallback callback(this);
    auto results = insti::orchestrator::verify(cmd.blueprint, &callback);

    post_to_ui(VerifyComplete{std::move(results)});
    m_busy.store(false);
}

void WorkerThread::do_refresh_registry(const RefreshRegistry& cmd)
{
    m_busy.store(true);
    m_cancel_requested.store(false);

    // Discover snapshots and blueprints from configured roots
    insti::SnapshotRegistry registry{ cmd.roots };
    registry.initialize();
    auto project_blueprints = registry.discover_project_blueprints();
    auto instance_blueprints = registry.discover_instance_blueprints();
    
    post_to_ui(RegistryRefreshComplete{true, std::move(project_blueprints), std::move(instance_blueprints)});
    m_busy.store(false);
}

} // namespace instinctiv
