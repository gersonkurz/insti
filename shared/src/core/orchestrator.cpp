#include "pch.h"
#include <insti/insti.h>

namespace insti
{

namespace
{

/// Run hooks for a specific phase.
/// @param bp Blueprint containing hooks
/// @param phase Phase to run hooks for
/// @param cb Callback for progress/errors (may be nullptr)
/// @param skip_all Reference to skip-all state (checked and updated)
/// @return true if all hooks succeeded or were skipped, false if aborted
bool run_hooks(const Blueprint* bp, Phase phase, IActionCallback* cb, bool& skip_all)
{
    const auto& hooks = bp->hooks(phase);
    if (hooks.empty())
        return true;

    const auto& vars = bp->resolved_variables();

    for (auto* hook : hooks)
    {
        if (cb)
            cb->on_progress(phase_to_string(phase), hook->type_name(), -1);

        // Set phase for hooks that need to know direction
        if (auto* sub = dynamic_cast<SubstituteHook*>(hook))
            sub->set_phase(phase);
        if (auto* sql = dynamic_cast<SqlHook*>(hook))
            sql->set_phase(phase);

        if (!hook->execute(vars))
        {
            if (skip_all)
                continue; // Skip without prompting

            if (cb)
            {
                auto decision = cb->on_error("Hook execution failed", hook->type_name());
                switch (decision)
                {
                case IActionCallback::Decision::Abort:
                    return false;
                case IActionCallback::Decision::SkipAll:
                    skip_all = true;
                    [[fallthrough]];
                case IActionCallback::Decision::Skip:
                case IActionCallback::Decision::Continue:
                case IActionCallback::Decision::Retry:
                default:
                    continue;
                }
            }
            else
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace

namespace orchestrator
{

bool backup(const Blueprint* bp, std::string_view output_path, IActionCallback* cb)
{
    if (!bp)
    {
        spdlog::error("backup: blueprint is null");
        return false;
    }

    spdlog::info("backup: starting backup to {}", output_path);

    bool skip_all = false;

    // Run PreBackup hooks
    spdlog::info("backup: running PreBackup hooks");
    if (!run_hooks(bp, Phase::PreBackup, cb, skip_all))
    {
        spdlog::error("backup: PreBackup hooks failed");
        return false;
    }
    spdlog::info("backup: PreBackup hooks completed");

    // Create snapshot writer
    ZipSnapshotWriter writer;
    std::string output_path_str{output_path};
    spdlog::info("backup: creating snapshot file");
    if (!writer.create(output_path_str))
    {
        spdlog::error("backup: failed to create snapshot file");
        if (cb)
            cb->on_error("Failed to create snapshot file", output_path);
        return false;
    }
    spdlog::info("backup: snapshot file created");

    // Create context
    auto* ctx = ActionContext::for_backup(bp, &writer, cb);
    ctx->set_skip_all_errors(skip_all);

    // Backup each action (forward order)
    bool success = true;
    const auto& actions = bp->actions();
    spdlog::info("backup: backing up {} actions", actions.size());

    int action_idx = 0;
    for (const auto* action : actions)
    {
        spdlog::info("backup: action {}/{}: {}", ++action_idx, actions.size(), action->description());
        if (!action->backup(ctx))
        {
            spdlog::error("backup: action failed: {}", action->description());
            success = false;
            break;
        }
        spdlog::info("backup: action completed: {}", action->description());
    }

    // Propagate skip_all state back for PostBackup hooks
    skip_all = ctx->skip_all_errors();
    ctx->release(REFCOUNT_DEBUG_ARGS);

    if (!success)
    {
        spdlog::error("backup: failed due to action failure");
        return false;
    }

    // Write blueprint to archive
    spdlog::info("backup: writing blueprint.xml to archive");
    if (!writer.write_text("blueprint.xml", bp->to_xml()))
    {
        spdlog::error("backup: failed to write blueprint.xml");
        if (cb)
            cb->on_error("Failed to write blueprint to archive", "blueprint.xml");
        return false;
    }

    // Finalize archive
    spdlog::info("backup: finalizing archive");
    if (!writer.finalize())
    {
        spdlog::error("backup: failed to finalize archive");
        if (cb)
            cb->on_error("Failed to finalize snapshot", output_path);
        return false;
    }

    // Run PostBackup hooks
    spdlog::info("backup: running PostBackup hooks");
    if (!run_hooks(bp, Phase::PostBackup, cb, skip_all))
    {
        spdlog::error("backup: PostBackup hooks failed");
        return false;
    }

    spdlog::info("backup: completed successfully");
    if (cb)
        cb->on_progress("Backup", "Complete", 100);

    return true;
}

bool restore(std::string_view archive_path, IActionCallback* cb, bool simulate)
{
    // Open archive
    ZipSnapshotReader reader;
    std::string archive_path_str{archive_path};
    if (!reader.open(archive_path_str))
    {
        if (cb)
            cb->on_error("Failed to open snapshot", archive_path);
        return false;
    }

    // Read blueprint from archive
    std::string blueprint_xml = reader.read_text("blueprint.xml");
    if (blueprint_xml.empty())
    {
        if (cb)
            cb->on_error("No blueprint.xml in snapshot", archive_path);
        return false;
    }

    auto* bp = Blueprint::load_from_string(blueprint_xml);
    if (!bp)
    {
        if (cb)
            cb->on_error("Failed to parse blueprint", archive_path);
        return false;
    }

    bool result = restore(bp, archive_path, cb, simulate);
    bp->release(REFCOUNT_DEBUG_ARGS);
    return result;
}

bool restore(const Blueprint* bp, std::string_view archive_path, IActionCallback* cb, bool simulate)
{
    if (!bp)
        return false;

    bool skip_all = false;

    // Open archive
    ZipSnapshotReader reader;
    std::string archive_path_str{archive_path};
    if (!reader.open(archive_path_str))
    {
        if (cb)
            cb->on_error("Failed to open snapshot", archive_path);
        return false;
    }

    // Run PreRestore hooks (skip in simulate mode)
    if (!simulate && !run_hooks(bp, Phase::PreRestore, cb, skip_all))
        return false;

    // Clean existing resources (reverse order)
    auto* clean_ctx = ActionContext::for_clean(bp, cb);
    clean_ctx->set_skip_all_errors(skip_all);
    clean_ctx->set_simulate(simulate);
    const auto& actions = bp->actions();

    for (auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        if (!(*it)->clean(clean_ctx))
        {
            clean_ctx->release(REFCOUNT_DEBUG_ARGS);
            return false;
        }
    }

    skip_all = clean_ctx->skip_all_errors();
    clean_ctx->release(REFCOUNT_DEBUG_ARGS);

    // Restore each action (forward order)
    auto* ctx = ActionContext::for_restore(bp, &reader, cb);
    ctx->set_skip_all_errors(skip_all);
    ctx->set_simulate(simulate);

    bool success = true;
    for (const auto* action : actions)
    {
        if (!action->restore(ctx))
        {
            success = false;
            break;
        }
    }

    skip_all = ctx->skip_all_errors();
    ctx->release(REFCOUNT_DEBUG_ARGS);

    if (!success)
        return false;

    // Run PostRestore hooks (skip in simulate mode)
    if (!simulate && !run_hooks(bp, Phase::PostRestore, cb, skip_all))
        return false;

    if (cb)
        cb->on_progress("Restore", "Complete", 100);

    return true;
}

bool clean(const Blueprint* bp, IActionCallback* cb, bool simulate)
{
    if (!bp)
        return false;

    bool skip_all = false;

    // Run PreClean hooks (skip in simulate mode)
    if (!simulate && !run_hooks(bp, Phase::PreClean, cb, skip_all))
        return false;

    // Create context
    auto* ctx = ActionContext::for_clean(bp, cb);
    ctx->set_skip_all_errors(skip_all);
    ctx->set_simulate(simulate);

    // Clean each action (reverse order)
    bool success = true;
    const auto& actions = bp->actions();

    for (auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        if (!(*it)->clean(ctx))
        {
            success = false;
            break;
        }
    }

    skip_all = ctx->skip_all_errors();
    ctx->release(REFCOUNT_DEBUG_ARGS);

    // Run PostClean hooks (even if clean had failures, skip in simulate mode)
    if (!simulate)
        run_hooks(bp, Phase::PostClean, cb, skip_all);

    return success;
}

std::vector<VerifyResult> verify(const Blueprint* bp, IActionCallback* cb)
{
    std::vector<VerifyResult> results;

    if (!bp)
        return results;

    auto* ctx = ActionContext::for_clean(bp, cb);

    for (const auto* action : bp->actions())
    {
        if (cb)
            cb->on_progress("Verify", action->description().c_str(), -1);

        results.push_back(action->verify(ctx));
    }

    ctx->release(REFCOUNT_DEBUG_ARGS);
    return results;
}

} // namespace orchestrator

// =============================================================================
// AbortOnErrorCallback
// =============================================================================

void AbortOnErrorCallback::on_progress(std::string_view phase, std::string_view detail, int /*percent*/)
{
    spdlog::info("[{}] {}", phase, detail);
}

void AbortOnErrorCallback::on_warning(std::string_view message)
{
    spdlog::warn("{}", message);
}

IActionCallback::Decision AbortOnErrorCallback::on_error(std::string_view message, std::string_view context)
{
    spdlog::error("{}: {}", message, context);
    return Decision::Abort;
}

IActionCallback::Decision AbortOnErrorCallback::on_file_conflict(std::string_view path, std::string_view action)
{
    spdlog::warn("File conflict: {} ({})", path, action);
    return Decision::Continue; // Overwrite by default
}

} // namespace insti
