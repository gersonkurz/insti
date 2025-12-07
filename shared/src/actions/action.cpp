#include "pch.h"
#include <insti/actions/action.h>
#include <insti/core/action_context.h>
#include <insti/snapshot/reader.h>

namespace insti
{

    bool IAction::handle_decision(IActionCallback::Decision decision, ActionContext *ctx)
    {
        switch (decision)
        {
        case IActionCallback::Decision::Continue:
        case IActionCallback::Decision::Skip:
            return true;
        case IActionCallback::Decision::SkipAll:
            ctx->set_skip_all_errors(true);
            return true;
        case IActionCallback::Decision::Abort:
        case IActionCallback::Decision::Retry:
        default:
            return false;
        }
    }

    bool IAction::check_archive_exists(const std::string &archive_path, ActionContext *ctx)
    {
        if (ctx->reader()->exists(archive_path))
            return true;

        if (ctx->skip_all_errors())
            return false;

        auto *cb = ctx->callback();
        if (cb)
        {
            auto decision = cb->on_error("Archive path does not exist in snapshot", archive_path);
            return handle_decision(decision, ctx);
        }

        spdlog::warn("Archive path does not exist in snapshot: {}", archive_path);
        return false;
    }

    bool IAction::clean(ActionContext *ctx) const
    {
        auto *cb = ctx->callback();

        if (cb)
            cb->on_progress("Clean", description(), -1);

        const bool success = do_clean(ctx);
        if (!success && cb)
        {
            auto decision = cb->on_error(m_type_name, m_description);
            return handle_decision(decision, ctx);
        }

        return success;
    }

} // namespace insti
