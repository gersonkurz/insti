#include "pch.h"
#include <insti/hooks/sql.h>
#include <sqlite3.h>
#include <pnq/sqlite/sqlite.h>

namespace insti
{

bool SqlHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    // Resolve variables in file path and query
    pnq::string::Expander expander{variables};
    expander.expand_dollar(true);
    expander.expand_percent(true);

    std::string resolved_path = expander.expand(m_file_path);
    std::string resolved_query = expander.expand(m_query);

    spdlog::debug("SqlHook: executing on {} query: {}", resolved_path, resolved_query);

    // Open database
    pnq::sqlite::Database db;
    if (!db.open(resolved_path))
    {
        spdlog::error("SqlHook: failed to open database: {}", resolved_path);
        return false;
    }

    // Execute query
    if (!db.execute(resolved_query))
    {
        spdlog::error("SqlHook: query failed: {}", db.last_error());
        return false;
    }

    spdlog::info("SqlHook: executed successfully ({} rows affected)", db.changes_count());
    return true;
}

} // namespace insti
