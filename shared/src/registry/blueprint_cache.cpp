#include "pch.h"
#include <insti/registry/blueprint_cache.h>
#include <algorithm>

namespace insti
{

BlueprintCache::BlueprintCache() = default;

BlueprintCache::~BlueprintCache()
{
    close();
}

bool BlueprintCache::open(std::string_view path)
{
    close();

    if (!m_db.open(path))
    {
        spdlog::error("BlueprintCache: failed to open database at {}", path);
        return false;
    }

    ensure_schema();
    spdlog::info("BlueprintCache: opened database at '{}'", path);
    return true;
}

bool BlueprintCache::open_default()
{
    std::string path = default_path();

    // Ensure directory exists
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    return open(path);
}

void BlueprintCache::close()
{
    if (is_open())
    {
        m_db.close();
    }
}

bool BlueprintCache::is_open() const
{
    return m_db.is_valid();
}

std::optional<std::string> BlueprintCache::get(std::string_view path, int64_t mtime, int64_t size, InstallStatus& install_status)
{
    if (!is_open())
    {
        spdlog::warn("BlueprintCache::get: cache not open!");
        return std::nullopt;
    }

    std::string normalized = normalize_path(path);

    pnq::sqlite::Statement stmt{ m_db, "SELECT mtime, size, xml, install_status FROM blueprints WHERE path = ?" };
    stmt.bind(normalized);

    if (!stmt.execute() || stmt.is_empty())
    {
        spdlog::info("BlueprintCache::get: no entry for '{}'", normalized);
        return std::nullopt;
    }

    int64_t cached_mtime = stmt.get_int64(0);
    int64_t cached_size = stmt.get_int64(1);
    std::string cached_status = stmt.get_text(3);

    spdlog::info("BlueprintCache::get: found '{}' with status='{}' (mtime match={}, size match={})",
                 normalized, cached_status, cached_mtime == mtime, cached_size == size);

    // Check if cache is stale (mtime/size changed)
    // But PRESERVE the install_status even if stale - the status is independent of file content
    install_status = install_status_from_string(cached_status);

    if (cached_mtime != mtime || cached_size != size)
    {
        spdlog::debug("BlueprintCache: stale XML for {} (mtime: {} vs {}, size: {} vs {}), but preserving status={}",
                      path, cached_mtime, mtime, cached_size, size, cached_status);
        return std::nullopt;
    }

    return stmt.get_text(2);
}

bool BlueprintCache::put(std::string_view path, int64_t mtime, int64_t size, std::string_view xml, InstallStatus install_status)
{
    if (!is_open())
        return false;

    const auto normalized{ normalize_path(path) };

    pnq::sqlite::Statement stmt{ m_db,
        "INSERT OR REPLACE INTO blueprints (path, mtime, size, xml, install_status) VALUES (?, ?, ?, ?, ?)" };
    stmt.bind(normalized);
    stmt.bind(mtime);
    stmt.bind(size);
    stmt.bind(xml);
    stmt.bind(as_string(install_status));
    return stmt.execute();
}

bool BlueprintCache::update_install_status(std::string_view path, InstallStatus install_status)
{
    if (!is_open())
        return false;

    const auto normalized{ normalize_path(path) };

    pnq::sqlite::Statement stmt{ m_db,
        "UPDATE blueprints SET install_status = ? WHERE path = ?" };
    stmt.bind(as_string(install_status));
    stmt.bind(normalized);
    return stmt.execute();
}

int BlueprintCache::mark_all_instances_not_installed(std::string_view project_name)
{
    if (!is_open())
        return 0;

    // Only update .zip files (instances), never .xml files (projects)
    // Use INSTR for more reliable matching - looks for name="ProjectName" in XML
    std::string search_str = std::format("name=\"{}\"", project_name);

    pnq::sqlite::Statement stmt{ m_db,
        "UPDATE blueprints SET install_status = ? WHERE path LIKE '%.zip' AND INSTR(xml, ?) > 0" };
    stmt.bind(as_string(InstallStatus::NotInstalled));
    stmt.bind(search_str);

    if (!stmt.execute())
    {
        spdlog::error("BlueprintCache: failed to mark instances of '{}' as NotInstalled", project_name);
        return 0;
    }

    spdlog::info("BlueprintCache: marked instances of '{}' as NotInstalled", project_name);
    return 1;
}

void BlueprintCache::remove(std::string_view path)
{
    if (!is_open())
        return;

    std::string normalized = normalize_path(path);

    pnq::sqlite::Statement stmt{ m_db, "DELETE FROM blueprints WHERE path = ?" };
    stmt.bind(normalized);
    stmt.execute();
}

void BlueprintCache::clear()
{
    if (!is_open())
        return;

    m_db.execute("DELETE FROM blueprints");
    spdlog::info("BlueprintCache: cleared all entries");
}

std::string BlueprintCache::default_path()
{
    char localappdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localappdata)))
    {
        return std::string(localappdata) + "\\insti\\cache.db";
    }
    return "cache.db";
}

void BlueprintCache::ensure_schema()
{
    if (!m_db.table_exists("blueprints"))
    {
        m_db.execute(R"(
            CREATE TABLE blueprints (
                path TEXT PRIMARY KEY,
                mtime INTEGER NOT NULL,
                size INTEGER NOT NULL,
                xml TEXT NOT NULL,
                install_status TEXT NOT NULL
            )
        )");
        spdlog::info("BlueprintCache: created schema");
    }
}

std::string BlueprintCache::normalize_path(std::string_view path)
{
    std::string result(path);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace insti
