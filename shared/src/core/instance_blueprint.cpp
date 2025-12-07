#include "pch.h"
#include <insti/core/instance_blueprint.h>
#include <insti/snapshot/zip_reader.h>
#include <pugixml.hpp>
#include <iomanip>
#include <sstream>

namespace insti
{

// InstanceMetadata implementation

std::string InstanceMetadata::timestamp_string() const
{
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm{};
    localtime_s(&tm, &time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::chrono::system_clock::time_point InstanceMetadata::parse_timestamp(std::string_view str)
{
    std::tm tm{};
    std::istringstream iss{std::string{str}};
    iss >> std::get_time(&tm, "%Y%m%d-%H%M%S");

    if (iss.fail())
        return {};

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// InstanceBlueprint implementation

InstanceBlueprint* InstanceBlueprint::load_from_archive(std::string_view zip_path)
{
    std::string path_str{zip_path};

    // Open the archive
    auto reader = new ZipSnapshotReader();
    if (!reader->open(path_str))
    {
        spdlog::error("Failed to open archive: {}", zip_path);
        reader->release(REFCOUNT_DEBUG_ARGS);
        return nullptr;
    }

    // Read blueprint.xml from archive root
    std::string xml = reader->read_text("blueprint.xml");
    if (xml.empty())
    {
        spdlog::error("Archive missing blueprint.xml: {}", zip_path);
        reader->release(REFCOUNT_DEBUG_ARGS);
        return nullptr;
    }

    reader->release(REFCOUNT_DEBUG_ARGS);

    return load_from_string(xml, zip_path);
}

InstanceBlueprint* InstanceBlueprint::load_from_string(std::string_view xml, std::string_view snapshot_path)
{
    std::string path_str{snapshot_path};

    auto bp = new InstanceBlueprint();
    bp->m_snapshot_path = path_str;
    bp->m_source_path = path_str;
    bp->populate_builtins();

    if (!bp->parse_xml(xml))
    {
        bp->release(REFCOUNT_DEBUG_ARGS);
        return nullptr;
    }

    // Parse instance metadata from XML
    pugi::xml_document doc;
    doc.load_string(std::string{xml}.c_str());
    auto root = doc.child("blueprint");

    if (!bp->parse_instance_metadata(&root))
    {
        // No instance metadata - use current machine values as fallback
        bp->m_instance.timestamp = std::chrono::system_clock::now();

        const auto& vars = bp->resolved_variables();
        auto it = vars.find("COMPUTERNAME");
        if (it != vars.end())
            bp->m_instance.machine = it->second;
        it = vars.find("USERNAME");
        if (it != vars.end())
            bp->m_instance.user = it->second;
    }

    return bp;
}

bool InstanceBlueprint::parse_instance_metadata(const void* root_ptr)
{
    const auto& root = *static_cast<const pugi::xml_node*>(root_ptr);

    auto instance_node = root.child("instance");
    if (!instance_node)
        return false;

    // Parse timestamp
    std::string timestamp_str = instance_node.attribute("timestamp").as_string();
    if (!timestamp_str.empty())
        m_instance.timestamp = InstanceMetadata::parse_timestamp(timestamp_str);

    // Parse machine
    m_instance.machine = instance_node.attribute("machine").as_string();

    // Parse user
    m_instance.user = instance_node.attribute("user").as_string();

    // Parse description (as child element for longer text)
    if (auto desc = instance_node.child("description"))
        m_instance.description = desc.text().as_string();

    return true;
}

std::string InstanceBlueprint::to_xml() const
{
    // Get base XML
    std::string base_xml = Blueprint::to_xml();

    // Parse it to insert instance metadata
    pugi::xml_document doc;
    doc.load_string(base_xml.c_str());

    auto root = doc.child("blueprint");
    if (!root)
        return base_xml;

    // Insert <instance> after <description> (or as first child if no description)
    pugi::xml_node instance_node;
    if (auto desc = root.child("description"))
        instance_node = root.insert_child_after("instance", desc);
    else
        instance_node = root.prepend_child("instance");

    // Add attributes
    instance_node.append_attribute("timestamp") = m_instance.timestamp_string().c_str();

    if (!m_instance.machine.empty())
        instance_node.append_attribute("machine") = m_instance.machine.c_str();

    if (!m_instance.user.empty())
        instance_node.append_attribute("user") = m_instance.user.c_str();

    // Add description as child element if present
    if (!m_instance.description.empty())
        instance_node.append_child("description").text().set(m_instance.description.c_str());

    std::ostringstream oss;
    doc.save(oss, "    ");
    return oss.str();
}

} // namespace insti
