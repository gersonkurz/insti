#include "pch.h"
#include "app_state.h"
#include "settings.h"
#include <algorithm>
#include <sstream>

namespace instinctiv
{

	AppState& AppState::instance()
	{
		static AppState state;
		return state;
	}

	AppState::~AppState()
	{
		clear_entries();
	}

	void AppState::initialize()
	{
		// Load registry roots from settings
		auto& registrySettings = config::theSettings.registry;
		const std::string root_strings = registrySettings.roots;
		registry_roots = pnq::string::split(root_strings, ";");

		// Create worker thread
		worker = std::make_unique<WorkerThread>();

		// Trigger initial registry refresh
		is_refreshing = true;
		status_message = "Scanning for snapshots...";
		worker->post(RefreshRegistry{ registry_roots });
	}

	void AppState::shutdown()
	{
		// Release active blueprint if any
		if (active_blueprint)
		{
			active_blueprint->release(REFCOUNT_DEBUG_ARGS);
			active_blueprint = nullptr;
		}

		worker.reset();
		clear_entries();
	}

	void AppState::clear_entries()
	{
		selected_snapshot = nullptr;
	}

} // namespace instinctiv
