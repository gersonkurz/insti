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

    m_db = std::make_unique<pnq::sqlite::Database>();
    if (!m_db->open(path))
    {
        spdlog::error("BlueprintCache: failed to open database at {}", path);
        m_db.reset();
        return false;
    }

    ensure_schema();
    spdlog::info("BlueprintCache: opened at {}", path);
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
    if (m_db)
    {
        m_db->close();
        m_db.reset();
    }
}

bool BlueprintCache::is_open() const
{
    return m_db && m_db->is_valid();
}

std::optional<std::string> BlueprintCache::get(std::string_view path, int64_t mtime, int64_t size)
{
    if (!is_open())
        return std::nullopt;

    std::string normalized = normalize_path(path);

    pnq::sqlite::Statement stmt(*m_db, "SELECT mtime, size, xml FROM blueprints WHERE path = ?");
    stmt.bind(normalized);

    if (!stmt.execute() || stmt.is_empty())
        return std::nullopt;

    int64_t cached_mtime = stmt.get_int64(0);
    int64_t cached_size = stmt.get_int64(1);

    // Check if cache is stale
    if (cached_mtime != mtime || cached_size != size)
    {
        spdlog::debug("BlueprintCache: stale entry for {} (mtime: {} vs {}, size: {} vs {})",
                      path, cached_mtime, mtime, cached_size, size);
        return std::nullopt;
    }

    return stmt.get_text(2);
}

void BlueprintCache::put(std::string_view path, int64_t mtime, int64_t size, std::string_view xml)
{
    if (!is_open())
        return;

    std::string normalized = normalize_path(path);

    pnq::sqlite::Statement stmt(*m_db,
        "INSERT OR REPLACE INTO blueprints (path, mtime, size, xml) VALUES (?, ?, ?, ?)");
    stmt.bind(normalized);
    stmt.bind(mtime);
    stmt.bind(size);
    stmt.bind(xml);
    stmt.execute();

    spdlog::debug("BlueprintCache: cached {}", path);
}

void BlueprintCache::remove(std::string_view path)
{
    if (!is_open())
        return;

    std::string normalized = normalize_path(path);

    pnq::sqlite::Statement stmt(*m_db, "DELETE FROM blueprints WHERE path = ?");
    stmt.bind(normalized);
    stmt.execute();
}

void BlueprintCache::clear()
{
    if (!is_open())
        return;

    m_db->execute("DELETE FROM blueprints");
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
    if (!m_db->table_exists("blueprints"))
    {
        m_db->execute(R"(
            CREATE TABLE blueprints (
                path TEXT PRIMARY KEY,
                mtime INTEGER NOT NULL,
                size INTEGER NOT NULL,
                xml TEXT NOT NULL
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
