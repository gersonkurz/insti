#pragma once

// =============================================================================
// insti/core/instance.h - Instance blueprint (from snapshot)
// =============================================================================

#include <insti/core/project.h>
#include <chrono>
#include <string>

namespace insti
{

	/// Installation status for display.
	enum class InstallStatus : uint32_t
	{
		Unknown = 0,			// Not checked
		NotInstalled,       // No version installed
		Installed,          // This exact version installed
		DifferentVersion    // Different version installed,
	};

	std::string as_string(InstallStatus status);
	InstallStatus install_status_from_string(std::string_view str);

	/// Instance blueprint - a captured state loaded from a snapshot archive.
	///
	/// Extends Project with snapshot path and instance metadata.
	/// Use the static factory method to create instances.
	class Instance final : public Project
	{
	public:
		/// Load instance blueprint from a snapshot archive.
		/// @param zip_path Path to the .zip snapshot file
		/// @return Instance pointer on success (caller owns ref), nullptr on failure
		static Instance* load_from_archive(std::string_view zip_path);

		/// Load instance blueprint from an XML string.
		/// @param xml XML content (with <instance> section)
		/// @param snapshot_path Path to the .zip archive to associate
		/// @return Instance pointer on success (caller owns ref), nullptr on failure
		static Instance* load_from_string(std::string_view xml, std::string_view snapshot_path);

	public:

		// Path to the .zip archive (not the project XML, which is inside the archive)
		std::string m_snapshot_path{};

		// When backup was taken
		std::chrono::system_clock::time_point m_timestamp{};

		// COMPUTERNAME at backup time
		std::string m_machine{};

		// USERNAME at backup time
		std::string m_user{};

		// User-provided description
		std::string m_description{};

		// Installation status on current machine, from cache database/verify
		InstallStatus m_install_status{ InstallStatus::Unknown };

		/// Serialize to XML, including instance metadata section.
		std::string to_xml() const override;

	public:

		/// Format timestamp as string (YYYY.MM.DD HH:MM:SS).
		std::string timestamp_string() const;

		/// Parse timestamp from string (YYYY.MM.DD HH:MM:SS).
		static std::chrono::system_clock::time_point parse_timestamp(std::string_view str);

	protected:
		Instance() = default;
		~Instance() override = default;

		/// Parse instance metadata from XML root node.
		/// @param root The <blueprint> root element
		/// @return true if <instance> section found and parsed, false otherwise
		bool parse_instance_metadata(const void* root);
	};

} // namespace insti
