#pragma once

// =============================================================================
// insti/core/instance_blueprint.h - Instance blueprint (from snapshot)
// =============================================================================

#include <insti/core/project_blueprint.h>
#include <chrono>
#include <string>

namespace insti
{

/// Metadata captured when creating a snapshot.
struct InstanceMetadata
{
    std::chrono::system_clock::time_point timestamp{};  ///< When backup was taken
    std::string machine{};       ///< COMPUTERNAME at backup time
    std::string user{};          ///< USERNAME at backup time
    std::string description{};   ///< User-provided description

    /// Format timestamp as string (YYYYMMDD-HHMMSS).
    std::string timestamp_string() const;

    /// Parse timestamp from string (YYYYMMDD-HHMMSS).
    static std::chrono::system_clock::time_point parse_timestamp(std::string_view str);
};

/// Instance blueprint - a captured state loaded from a snapshot archive.
///
/// Extends ProjectBlueprint with snapshot path and instance metadata.
/// Use the static factory method to create instances.
class InstanceBlueprint : public ProjectBlueprint
{
public:
    /// Load instance blueprint from a snapshot archive.
    /// @param zip_path Path to the .zip snapshot file
    /// @return InstanceBlueprint pointer on success (caller owns ref), nullptr on failure
    static InstanceBlueprint* load_from_archive(std::string_view zip_path);

    /// Load instance blueprint from an XML string.
    /// @param xml XML content (with <instance> section)
    /// @param snapshot_path Path to the .zip archive to associate
    /// @return InstanceBlueprint pointer on success (caller owns ref), nullptr on failure
    static InstanceBlueprint* load_from_string(std::string_view xml, std::string_view snapshot_path);

    /// Get the snapshot archive path.
    const std::string& snapshot_path() const { return m_snapshot_path; }

    /// Get the instance metadata.
    const InstanceMetadata& instance() const { return m_instance; }

    /// Get mutable instance metadata (for populating during backup).
    InstanceMetadata& instance() { return m_instance; }

    /// Serialize to XML, including instance metadata section.
    std::string to_xml() const override;

protected:
    InstanceBlueprint() = default;
    ~InstanceBlueprint() override = default;

    /// Parse instance metadata from XML root node.
    /// @param root The <blueprint> root element
    /// @return true if <instance> section found and parsed, false otherwise
    bool parse_instance_metadata(const void* root);

    std::string m_snapshot_path;   ///< Path to the .zip archive
    InstanceMetadata m_instance;   ///< Capture metadata
};

} // namespace insti
