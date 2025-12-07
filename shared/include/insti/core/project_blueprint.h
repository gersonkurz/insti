#pragma once

// =============================================================================
// insti/core/project_blueprint.h - Project blueprint (standalone .xml)
// =============================================================================

#include <insti/core/blueprint.h>
#include <string>

namespace insti
{

/// Project blueprint - a reusable template loaded from a standalone .xml file.
///
/// Extends Blueprint with the source path where it was loaded from.
/// Use the static factory method to create instances.
class ProjectBlueprint : public Blueprint
{
public:
    /// Load project blueprint from an XML file.
    /// @param xml_path Path to the .xml blueprint file
    /// @return ProjectBlueprint pointer on success (caller owns ref), nullptr on failure
    static ProjectBlueprint* load_from_file(std::string_view xml_path);

    /// Load project blueprint from an XML string.
    /// @param xml XML content
    /// @param source_path Path to associate with this blueprint (for display/tracking)
    /// @return ProjectBlueprint pointer on success (caller owns ref), nullptr on failure
    static ProjectBlueprint* load_from_string(std::string_view xml, std::string_view source_path);

    bool matches(std::string_view filter_text) const
    {
        std::string lower_filter = pnq::string::lowercase(filter_text);
        return pnq::string::contains(pnq::string::lowercase(name()), lower_filter) ||
               pnq::string::contains(pnq::string::lowercase(m_source_path), lower_filter) ||
               pnq::string::contains(pnq::string::lowercase(description()), lower_filter);
    }

    /// Get the source file path.
    const std::string& source_path() const { return m_source_path; }

protected:
    ProjectBlueprint() = default;
    ~ProjectBlueprint() override = default;

    std::string m_source_path;  ///< Path to the .xml file this was loaded from
};

} // namespace insti
