#pragma once

// =============================================================================
// insti/registry/blueprint_cache.h - SQLite cache for parsed blueprints
// =============================================================================

#include <string>
#include <string_view>
#include <optional>
#include <cstdint>

namespace pnq::sqlite { class Database; }

namespace insti
{

/// SQLite-backed cache for blueprint XML.
/// Caches serialized blueprint XML keyed by file path, with mtime/size for invalidation.
class BlueprintCache
{
public:
    BlueprintCache();
    ~BlueprintCache();

    /// Open the cache database.
    /// @param path Path to SQLite database file (created if doesn't exist)
    /// @return true on success
    bool open(std::string_view path);

    /// Open the cache at the default location (%LOCALAPPDATA%\insti\cache.db).
    /// @return true on success
    bool open_default();

    /// Close the cache database.
    void close();

    /// Check if cache is open.
    bool is_open() const;

    /// Get cached XML for a file path.
    /// @param path File path (will be lowercased for lookup)
    /// @param mtime File modification time (for invalidation check)
    /// @param size File size (for invalidation check)
    /// @return Cached XML if valid cache hit, std::nullopt if miss or stale
    std::optional<std::string> get(std::string_view path, int64_t mtime, int64_t size);

    /// Store XML in cache.
    /// @param path File path (will be lowercased for storage)
    /// @param mtime File modification time
    /// @param size File size
    /// @param xml Serialized blueprint XML
    void put(std::string_view path, int64_t mtime, int64_t size, std::string_view xml);

    /// Remove a cache entry.
    /// @param path File path (will be lowercased)
    void remove(std::string_view path);

    /// Clear all cache entries.
    void clear();

    /// Get the default cache path (%LOCALAPPDATA%\insti\cache.db).
    static std::string default_path();

private:
    void ensure_schema();
    static std::string normalize_path(std::string_view path);

    std::unique_ptr<pnq::sqlite::Database> m_db;
};

} // namespace insti
