#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <pnq/ref_counted.h>
#include "entry.h"

namespace insti
{

/// Abstract base class for reading snapshots.
/// @note Cannot be final - has virtual destructor for polymorphism.
class SnapshotReader : public pnq::RefCountImpl
{
public:
    virtual ~SnapshotReader() = default;

    // --- Pure virtual (implementations provide) ---

    /// Get flat list of all entry paths (using / separator).
    virtual std::vector<std::string> get_all_paths() const = 0;

    /// Read file content as binary.
    /// @param path Path within archive (using / separator)
    virtual std::vector<uint8_t> read_binary(std::string_view path) const = 0;

    /// Extract file to disk.
    /// @param archive_path Path within archive (using / separator)
    /// @param dest_path Destination file path on disk
    virtual bool extract_to_file(std::string_view archive_path, std::string_view dest_path) const = 0;

    /// Close the snapshot and release resources.
    virtual void close() = 0;

    /// Check if snapshot is open.
    virtual bool is_open() const = 0;

    // --- ABC provides (built on cached path tree) ---

    /// Extract a directory tree from archive to disk.
    /// @param archive_prefix Path prefix in archive (e.g. "files/myapp")
    /// @param dest_dir Destination directory on disk
    bool extract_directory_recursive(std::string_view archive_prefix, std::string_view dest_dir) const;

    /// Check if path exists in archive.
    /// @param path Path within archive (using / separator)
    bool exists(std::string_view path) const;

    /// Check if path is a directory.
    /// @param path Path within archive (using / separator)
    bool is_directory(std::string_view path) const;

    /// List immediate children of a directory.
    /// @param path Directory path within archive (using / separator)
    std::vector<std::string> list_dir(std::string_view path) const;

    /// Read file content as text.
    /// @param path Path within archive (using / separator)
    std::string read_text(std::string_view path) const;

    /// Get all entries.
    std::vector<ArchiveEntry> entries() const;

    /// Forward iterator for archive entries.
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = ArchiveEntry;
        using difference_type = std::ptrdiff_t;
        using pointer = const ArchiveEntry*;
        using reference = const ArchiveEntry&;

        Iterator(const SnapshotReader* reader, size_t index);

        ArchiveEntry operator*() const;
        Iterator& operator++();
        Iterator operator++(int);
        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const;

    private:
        const SnapshotReader* m_reader;  ///< Parent reader (non-owning)
        size_t m_index;                   ///< Current position in ordered paths
    };

    Iterator begin() const;
    Iterator end() const;
    size_t size() const;

protected:
    /// Build path tree from get_all_paths() - call once after open.
    void build_path_cache() const;

private:
    mutable bool m_cache_built = false;                                         ///< Whether cache has been built
    mutable std::unordered_set<std::string> m_all_paths;                        ///< All paths in archive
    mutable std::unordered_set<std::string> m_directories;                      ///< Directory paths only
    mutable std::unordered_map<std::string, std::vector<std::string>> m_children; ///< Parent -> children map
    mutable std::vector<std::string> m_ordered_paths;                           ///< Paths in iteration order
};

} // namespace insti
