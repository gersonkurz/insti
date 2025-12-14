#include "pch.h"
#include <insti/insti.h>

namespace insti
{

	namespace
	{

		/// Run lifecycle hooks (startup or shutdown).
		/// @param hooks Vector of hooks to execute
		/// @param lifecycle_name Name for progress reporting ("Startup" or "Shutdown")
		/// @param vars Resolved variables for hook execution
		/// @param cb Callback for progress/errors (may be nullptr)
		/// @param skip_all Reference to skip-all state (checked and updated)
		/// @param force If true, also run hooks marked as force-only
		/// @return true if all hooks succeeded or were skipped, false if aborted
		bool run_lifecycle_hooks(
			const pnq::RefCountedVector<IHook*>& hooks,
			const char* lifecycle_name,
			const std::unordered_map<std::string, std::string>& vars,
			IActionCallback* cb,
			bool& skip_all,
			bool force = false)
		{
			if (hooks.empty())
				return true;

			for (auto* hook : hooks)
			{
				// Skip force-only hooks unless force is enabled
				if (hook->is_force() && !force)
					continue;

				if (cb)
					cb->on_progress(lifecycle_name, hook->type_name(), -1);

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
						// No callback - treat shutdown failures as warnings, startup failures as errors
						if (std::string_view(lifecycle_name) == "Shutdown")
							continue; // Shutdown failures are non-fatal (app might not be running)
						return false;
					}
				}
			}

			return true;
		}

	} // namespace

	Orchestrator::Orchestrator(SnapshotRegistry* snapshot_registry)
			: m_snapshot_registry{ snapshot_registry }
		{
			PNQ_ADDREF(m_snapshot_registry);
		}

		Orchestrator::~Orchestrator()
		{
			PNQ_RELEASE(m_snapshot_registry);
		}

		bool Orchestrator::backup(const Project* bp, std::string_view output_path, IActionCallback* cb, bool force, const std::string& description)
		{
			if (!bp)
			{
				spdlog::error("backup: blueprint is null");
				return false;
			}

			spdlog::info("backup: starting backup to {}", output_path);

			bool skip_all = false;
			const auto& vars = bp->resolved_variables();

			// Shutdown before backup
			spdlog::info("backup: running shutdown hooks");
			if (!run_lifecycle_hooks(bp->shutdown_hooks(), "Shutdown", vars, cb, skip_all, force))
			{
				spdlog::error("backup: shutdown hooks failed");
				return false;
			}
			spdlog::info("backup: shutdown hooks completed");

			// Create snapshot writer
			ZipSnapshotWriter writer;
			std::string output_path_str{ output_path };
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

			// Write blueprint to archive with instance metadata
			spdlog::info("backup: writing blueprint.xml to archive");
			auto now = std::chrono::system_clock::now();
			std::string machine, user;
			auto it = vars.find("COMPUTERNAME");
			if (it != vars.end())
				machine = it->second;
			it = vars.find("USERNAME");
			if (it != vars.end())
				user = it->second;

			std::string instance_description = description.empty() ? bp->project_description() : description;
			std::string instance_xml = bp->to_instance_xml(now, machine, user, instance_description);
			if (!writer.write_text("blueprint.xml", instance_xml))
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

			// Startup after backup
			spdlog::info("backup: running startup hooks");
			if (!run_lifecycle_hooks(bp->startup_hooks(), "Startup", vars, cb, skip_all, force))
			{
				spdlog::error("backup: startup hooks failed");
				return false;
			}

			m_snapshot_registry->on_backup_complete(bp->project_name(), output_path);

			spdlog::info("backup: completed successfully");
			if (cb)
				cb->on_progress("Backup", "Complete", 100);

			return true;
		}

		bool Orchestrator::restore(const Instance* bp, std::string_view archive_path, IActionCallback* cb, bool simulate, bool force)
		{
			if (!bp)
				return false;

			bool skip_all = false;
			const auto& vars = bp->resolved_variables();

			// Open archive
			ZipSnapshotReader reader;
			std::string archive_path_str{ archive_path };
			if (!reader.open(archive_path_str))
			{
				if (cb)
					cb->on_error("Failed to open snapshot", archive_path);
				return false;
			}

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

			// Startup after restore (skip in simulate mode)
			if (!simulate && !run_lifecycle_hooks(bp->startup_hooks(), "Startup", vars, cb, skip_all, force))
				return false;

			if (!simulate && success)
			{
				m_snapshot_registry->on_restore_complete(bp->project_name(), archive_path);
			}
			if (cb)
				cb->on_progress("Restore", "Complete", 100);

			return true;
		}

		bool Orchestrator::clean(const Blueprint* bp, IActionCallback* cb, bool simulate, bool force)
		{
			if (!bp)
				return false;

			bool skip_all = false;
			const auto& vars = bp->resolved_variables();

			// Shutdown before clean (skip in simulate mode)
			if (!simulate && !run_lifecycle_hooks(bp->shutdown_hooks(), "Shutdown", vars, cb, skip_all, force))
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

			if (!simulate && success)
			{
				m_snapshot_registry->on_clean_complete(bp->project_name());
			}

			if (cb)
				cb->on_progress("Clean", "Complete", 100);

			return success;
		}

		std::vector<VerifyResult> Orchestrator::verify(const Blueprint* bp, IActionCallback* cb, SnapshotReader* reader)
		{
			std::vector<VerifyResult> results;

			if (!bp)
				return results;

			// Use restore context if reader is available (instance verification)
			// Otherwise use clean context (project verification - just checks existence)
			ActionContext* ctx = reader
				? ActionContext::for_restore(bp, reader, cb)
				: ActionContext::for_clean(bp, cb);

			for (const auto* action : bp->actions())
			{
				if (cb)
					cb->on_progress("Verify", action->description().c_str(), -1);

				results.push_back(action->verify(ctx));
			}

			ctx->release(REFCOUNT_DEBUG_ARGS);
			return results;
		}

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
