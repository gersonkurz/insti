#include "pch.h"
#include <insti/registry/snapshot_registry.h>
#include <insti/core/blueprint.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace insti
{

	namespace fs = std::filesystem;

	Instance* SnapshotRegistry::find_instance_for_path(std::string_view output_path)
	{
		// Search known instance blueprints for matching snapshot path (case-insensitive on Windows)
		for (auto* bp : m_instances)
		{
			if (pnq::string::equals_nocase(bp->m_snapshot_path, output_path))
			{
				PNQ_ADDREF(bp);
				return bp;
			}
		}
		return nullptr;
	}

	void SnapshotRegistry::on_backup_complete(std::string_view project_name, std::string_view output_path)
	{
		spdlog::info("on_backup_complete: project='{}', path='{}'", project_name, output_path);
		ensure_cache();

		// Check if there's already an installed instance for this project
		bool already_installed = false;
		for (auto* instance : m_instances)
		{
			if (instance->m_install_status == InstallStatus::Installed &&
				instance->project_name() == project_name)
			{
				already_installed = true;
				break;
			}
		}

		// Add the new instance to our list
		const fs::path path{ output_path };
		const fs::directory_entry dir_entry{ path };

		// Only mark as Installed if nothing else is installed for this project
		// (i.e., this is a fresh backup capturing current state, not a re-backup)
		initialize_instance_blueprint(dir_entry,
			already_installed ? InstallStatus::NotInstalled : InstallStatus::Installed);
	}

	void SnapshotRegistry::on_restore_complete(std::string_view project_name, std::string_view output_path)
	{
		spdlog::info("on_restore_complete: project='{}', path='{}'", project_name, output_path);

		// Mark ALL instances of this project as NotInstalled (in cache and in memory)
		ensure_cache();
		m_cache.mark_all_instances_not_installed(project_name);

		for (auto* instance : m_instances)
		{
			if (instance->project_name() == project_name)
			{
				spdlog::debug("on_restore_complete: marking '{}' as NotInstalled", instance->m_snapshot_path);
				instance->m_install_status = InstallStatus::NotInstalled;
			}
		}

		// Now mark the restored one as Installed
		auto* restored_instance = find_instance_for_path(output_path);
		if (restored_instance)
		{
			spdlog::info("on_restore_complete: marking '{}' as Installed", output_path);
			restored_instance->m_install_status = InstallStatus::Installed;
			m_cache.update_install_status(output_path, InstallStatus::Installed);
			PNQ_RELEASE(restored_instance);
		}
		else
		{
			spdlog::warn("on_restore_complete: snapshot '{}' not found in instance list", output_path);
		}
	}

	void SnapshotRegistry::on_clean_complete(std::string_view project_name)
	{
		spdlog::info("on_clean_complete: project='{}'", project_name);

		// Mark ALL instances of this project as NotInstalled (in cache and in memory)
		ensure_cache();
		m_cache.mark_all_instances_not_installed(project_name);

		for (auto* instance : m_instances)
		{
			if (instance->project_name() == project_name)
				instance->m_install_status = InstallStatus::NotInstalled;
		}
	}

	bool SnapshotRegistry::initialize()
	{
		ensure_cache();

		std::error_code ec;

		for (const auto& root : m_roots)
		{
			if (root.empty())
				continue;

			fs::path root_path{ root };
			if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec))
				continue;

			// Recursively find all .xml files
			for (const auto& dir_entry : fs::recursive_directory_iterator(root_path, ec))
			{
				if (ec)
				{
					spdlog::warn("Error iterating {}: {}", root, ec.message());
					break;
				}

				if (!dir_entry.is_regular_file())
					continue;

				const auto& path = dir_entry.path();
				const auto ext = pnq::string::lowercase(path.extension().string());
				if (ext == ".xml")
				{
					initialize_project_blueprint(dir_entry);
				}
				else if (ext == ".zip")
				{
					initialize_instance_blueprint(dir_entry, InstallStatus::Unknown);
				}
			}
		}

		// Sort by name
		std::sort(m_instances.begin(), m_instances.end(), [](const Instance* a, const Instance* b) {
			return a->project_name() < b->project_name();
			});

		std::sort(m_projects.begin(), m_projects.end(), [](const Project* a, const Project* b) {
			return a->project_name() < b->project_name();
			});
		return true;
	}

	bool SnapshotRegistry::initialize_project_blueprint(const fs::directory_entry& dir_entry) const
	{
		std::error_code ec;
		std::string path_str = dir_entry.path().string();
		int64_t mtime = std::chrono::duration_cast<std::chrono::seconds>(
			dir_entry.last_write_time(ec).time_since_epoch()).count();
		int64_t size = static_cast<int64_t>(dir_entry.file_size(ec));

		// Try cache first
		InstallStatus install_status{ InstallStatus::Unknown };
		auto cached_xml = m_cache.get(path_str, mtime, size, install_status);
		if (cached_xml)
		{
			// Parse from cached XML
			const auto bp = Project::load_from_string(*cached_xml, path_str);
			if (bp)
			{
				m_projects.push_back(bp);
				return true;
			}
		}

		// Cache miss or stale - load from file
		const auto bp = Project::load_from_file(path_str);
		if (bp)
		{
			// Cache the serialized XML
			m_cache.put(path_str, mtime, size, bp->to_xml(), InstallStatus::Unknown);
			m_projects.push_back(bp);
			return true;
		}
		return false;
	}

	bool SnapshotRegistry::initialize_instance_blueprint(const fs::directory_entry& dir_entry, InstallStatus default_status) const
	{
		std::error_code ec;
		std::string path_str = dir_entry.path().string();
		int64_t mtime = std::chrono::duration_cast<std::chrono::seconds>(
			dir_entry.last_write_time(ec).time_since_epoch()).count();
		int64_t size = static_cast<int64_t>(dir_entry.file_size(ec));

		// Try cache first - this sets install_status from cache even if XML is stale
		InstallStatus install_status = default_status;
		auto cached_xml = m_cache.get(path_str, mtime, size, install_status);

		spdlog::info("initialize_instance_blueprint: '{}' -> status={} (from cache: {})",
		             path_str, as_string(install_status), cached_xml.has_value() ? "hit" : "miss/stale");

		if (cached_xml)
		{
			// Parse from cached XML
			const auto bp = Instance::load_from_string(*cached_xml, path_str);
			if (bp)
			{
				bp->m_install_status = install_status;
				m_instances.push_back(bp);
				return true;
			}
			// Cache was corrupt/invalid, fall through to reload
		}

		// Cache miss or stale XML - load from archive
		// Note: install_status was already set by get() if entry exists (preserves status even if XML stale)
		const auto bp = Instance::load_from_archive(path_str);
		if (bp)
		{
			bp->m_install_status = install_status;
			m_cache.put(path_str, mtime, size, bp->to_xml(), bp->m_install_status);
			m_instances.push_back(bp);
			return true;
		}
		return false;
	}

	std::string SnapshotRegistry::generate_filename(std::string_view project,
		std::chrono::system_clock::time_point timestamp) const
	{
		// Format timestamp
		auto time = std::chrono::system_clock::to_time_t(timestamp);
		std::tm tm;
		localtime_s(&tm, &time);
		std::ostringstream ts_oss;
		ts_oss << std::put_time(&tm, "%Y%m%d-%H%M%S");

		return std::format("{}-{}.zip", project, ts_oss.str());
	}

	std::string SnapshotRegistry::first_writable_root() const
	{
		for (const auto& root : m_roots)
		{
			if (!root.empty())
			{
				return root;
			}
		}
		return "";
	}

	void SnapshotRegistry::ensure_cache() const
	{
		if (!m_cache.is_open())
			m_cache.open_default();
	}

	Instance* SnapshotRegistry::installed_instance() const
	{
		// If we have a cached result, use it
		if (m_installation_cache_valid && !m_cached_installed_path.empty())
		{
			// Load and return the cached installed blueprint
			return Instance::load_from_archive(m_cached_installed_path);
		}
		else if (m_installation_cache_valid)
		{
			// Cache says nothing is installed
			return nullptr;
		}

		// Discover all instance blueprints
		auto instances = m_instances;
		if (instances.empty())
		{
			m_cached_installed_path.clear();
			m_installation_cache_valid = true;
			return nullptr;
		}

		// Gather unique INSTALLDIR paths
		std::unordered_set<std::string> install_dirs;
		for (const auto* bp : instances)
		{
			std::string dir = bp->installdir();
			if (!dir.empty())
				install_dirs.insert(dir);
		}

		// Check each INSTALLDIR for blueprint.xml
		std::error_code ec;

		for (const auto& dir : install_dirs)
		{
			fs::path bp_path = fs::path{ dir } / "blueprint.xml";
			if (!fs::exists(bp_path, ec))
				continue;

			// Read the installed blueprint.xml
			std::string xml = pnq::text_file::read_auto(bp_path.string());
			if (xml.empty())
				continue;

			// Parse as instance blueprint (we need instance metadata to match)
			// Use empty snapshot_path since this is reading from filesystem, not archive
			auto* installed = Instance::load_from_string(xml, "");
			if (!installed)
				continue;

			// Find matching instance blueprint by comparing metadata
			for (auto* known : instances)
			{
				// Match by timestamp + machine + user (should be unique enough)
				if (installed->m_timestamp == known->m_timestamp &&
					installed->m_machine == known->m_machine &&
					installed->m_user == known->m_user)
				{
					// Found a match - cache it and return
					m_cached_installed_path = known->m_snapshot_path;
					m_installation_cache_valid = true;

					// Release all except the match
					for (auto* bp : instances)
					{
						if (bp != known)
							bp->release(REFCOUNT_DEBUG_ARGS);
					}
					installed->release(REFCOUNT_DEBUG_ARGS);

					return known;
				}
			}

			installed->release(REFCOUNT_DEBUG_ARGS);
		}

		// No match found - release all and cache negative result
		for (auto* bp : instances)
			bp->release(REFCOUNT_DEBUG_ARGS);

		m_cached_installed_path.clear();
		m_installation_cache_valid = true;
		return nullptr;
	}


	void SnapshotRegistry::notify_restore_complete(const std::string& /*install_dir*/)
	{
		// Invalidate installation cache since a new instance was just installed
		invalidate_installation_cache();
	}

	void SnapshotRegistry::notify_clean_complete()
	{
		// Invalidate installation cache since installation was removed
		invalidate_installation_cache();
	}

	void SnapshotRegistry::invalidate_installation_cache() const
	{
		m_cached_installed_path.clear();
		m_installation_cache_valid = false;
	}

} // namespace insti
