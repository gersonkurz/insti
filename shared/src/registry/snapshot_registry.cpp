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
					initialize_instance_blueprint(dir_entry);
				}
			}
		}

		// Sort by name
		std::sort(m_instance_blueprints.begin(), m_instance_blueprints.end(), [](const Instance* a, const Instance* b) {
			return a->name() < b->name();
			});

		std::sort(m_project_blueprints.begin(), m_project_blueprints.end(), [](const Project* a, const Project* b) {
			return a->name() < b->name();
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
				m_project_blueprints.push_back(bp);
				return true;
			}
		}

		// Cache miss or stale - load from file
		const auto bp = Project::load_from_file(path_str);
		if (bp)
		{
			// Cache the serialized XML
			m_cache.put(path_str, mtime, size, bp->to_xml(), InstallStatus::Unknown);
			m_project_blueprints.push_back(bp);
			return true;
		}
		return false;
	}

	bool SnapshotRegistry::initialize_instance_blueprint(const fs::directory_entry& dir_entry) const
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
			const auto bp = Instance::load_from_string(*cached_xml, path_str);
			if (bp)
			{
				bp->m_install_status = install_status;
				m_instance_blueprints.push_back(bp);
				return true;
			}
			// Cache was corrupt/invalid, fall through to reload
		}

		// Cache miss or stale - load from archive
		const auto bp = Instance::load_from_archive(path_str);
		if (bp)
		{
			// Cache the serialized XML
			m_cache.put(path_str, mtime, size, bp->to_xml(), bp->m_install_status);
			m_instance_blueprints.push_back(bp);
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
		auto instances = m_instance_blueprints;
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
