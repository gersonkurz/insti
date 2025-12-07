#pragma once

#include <insti/registry/blueprint_cache.h>
#include <insti/core/project_blueprint.h>
#include <insti/core/instance_blueprint.h>
#include <vector>
#include <string>
#include <string_view>
#include <memory>

namespace insti
{

	/// Registry for discovering and managing snapshots across multiple roots.
	class SnapshotRegistry final : public pnq::RefCountImpl
	{
	public:
		/// @param roots List of registry root directories
		/// @param naming Naming pattern for filenames (defaults to standard pattern)
		explicit SnapshotRegistry(const std::vector<std::string>& roots)
			: m_roots{ roots }
		{
		}

		bool initialize();


		/// Generate a filename from entry data using the configured pattern.
		std::string generate_filename(std::string_view project,
			std::chrono::system_clock::time_point timestamp) const;

		/// Get the first writable root, or empty if none.
		std::string first_writable_root() const;

		/// Get the currently installed instance, if any.
		/// Scans INSTALLDIR paths from known instance blueprints for blueprint.xml.
		/// Returns matching InstanceBlueprint* (caller must release) or nullptr if none installed.
		InstanceBlueprint* installed_instance() const;

		/// Notify registry that a backup completed and a new snapshot was created.
		/// @param snapshot_path Path to the newly created snapshot archive
		void notify_backup_complete(const std::string& snapshot_path);

		/// Notify registry that a restore completed.
		/// @param install_dir The INSTALLDIR where instance blueprint was written
		void notify_restore_complete(const std::string& install_dir);

		/// Notify registry that a clean completed.
		void notify_clean_complete();

		pnq::RefCountedVector<InstanceBlueprint*> discover_instances(std::string_view filter_text)
		{
			return discover<InstanceBlueprint>(filter_text, m_instance_blueprints);
		}

		pnq::RefCountedVector<ProjectBlueprint*> discover_projects(std::string_view filter_text)
		{
			return discover<ProjectBlueprint>(filter_text, m_project_blueprints);
		}

		mutable pnq::RefCountedVector<InstanceBlueprint*> m_instance_blueprints;
		mutable pnq::RefCountedVector<ProjectBlueprint*> m_project_blueprints;

	private:
		template <typename T> pnq::RefCountedVector<T*> discover(std::string_view filter_text, const pnq::RefCountedVector<T*>& blueprints)
		{
			pnq::RefCountedVector<T*> result;
			for (auto* bp : blueprints)
			{
				if (bp->matches(filter_text))
				{
					result.push_back(bp);
				}
			}
			return result;
		}


		const std::vector<std::string> m_roots;

		mutable BlueprintCache m_cache;  ///< Cache for parsed blueprints (mutable for const methods)
		mutable std::string m_cached_installed_path;  ///< Cached path of installed blueprint.xml (empty if none/unknown)
		mutable bool m_installation_cache_valid = false;  ///< Whether m_cached_installed_path is valid

		/// Ensure cache is initialized.
		void ensure_cache() const;

		/// Invalidate installation cache (forces re-scan on next installed_instance() call).
		void invalidate_installation_cache() const;


		bool initialize_project_blueprint(const fs::directory_entry& dir_entry) const;
		bool initialize_instance_blueprint(const fs::directory_entry& dir_entry) const;
	};

} // namespace insti

