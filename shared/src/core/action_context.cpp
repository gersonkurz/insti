#include "pch.h"
#include <insti/core/action_context.h>
#include <insti/core/action_callback.h>
#include <insti/core/blueprint.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>

namespace insti
{

#define ASSIGN_ADDREF(member, value) member = value; PNQ_ADDREF(value)

ActionContext::ActionContext(const Blueprint* blueprint, SnapshotReader* reader, SnapshotWriter* writer, IActionCallback* callback)
{
    ASSIGN_ADDREF(m_blueprint, blueprint);
    ASSIGN_ADDREF(m_reader, reader);
    ASSIGN_ADDREF(m_writer, writer);
    ASSIGN_ADDREF(m_callback, callback);
}

ActionContext* ActionContext::for_backup(const Blueprint* blueprint, SnapshotWriter* writer, IActionCallback* callback)
{
    return new ActionContext(blueprint, nullptr, writer, callback);
}

ActionContext* ActionContext::for_restore(const Blueprint* blueprint, SnapshotReader* reader, IActionCallback* callback)
{
    return new ActionContext(blueprint, reader, nullptr, callback);
}

ActionContext* ActionContext::for_clean(const Blueprint* blueprint, IActionCallback* callback)
{
    return new ActionContext(blueprint, nullptr, nullptr, callback);
}

ActionContext::~ActionContext()
{
    PNQ_RELEASE(m_blueprint);
    PNQ_RELEASE(m_reader);
    PNQ_RELEASE(m_writer);
    PNQ_RELEASE(m_callback);
}

void ActionContext::set_override(std::string_view name, std::string_view value)
{
    m_overrides[std::string{name}] = std::string{value};
    m_merged_dirty = true;
}

const std::unordered_map<std::string, std::string>& ActionContext::variables() const
{
    if (m_merged_dirty)
        rebuild_merged_variables();
    return m_merged_variables;
}

void ActionContext::rebuild_merged_variables() const
{
    m_merged_variables = m_blueprint->resolved_variables();
    for (const auto& [name, value] : m_overrides)
        m_merged_variables[name] = value;
    m_merged_dirty = false;
}

} // namespace insti
