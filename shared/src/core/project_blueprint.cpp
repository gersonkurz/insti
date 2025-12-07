#include "pch.h"
#include <insti/core/project_blueprint.h>

namespace insti
{

ProjectBlueprint* ProjectBlueprint::load_from_file(std::string_view xml_path)
{
    std::string path_str{xml_path};
    std::string content = pnq::text_file::read_auto(path_str);
    if (content.empty() && !pnq::file::exists(path_str))
    {
        spdlog::error("Failed to read file: {}", xml_path);
        return nullptr;
    }

    return load_from_string(content, xml_path);
}

ProjectBlueprint* ProjectBlueprint::load_from_string(std::string_view xml, std::string_view source_path)
{
    auto bp = new ProjectBlueprint();
    bp->m_source_path = std::string{source_path};
    bp->populate_builtins();

    if (!bp->parse_xml(xml))
    {
        bp->release(REFCOUNT_DEBUG_ARGS);
        return nullptr;
    }

    return bp;
}

} // namespace insti
