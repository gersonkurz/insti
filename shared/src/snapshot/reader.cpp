#include "pch.h"
#include <insti/snapshot/reader.h>

namespace insti
{

void SnapshotReader::build_path_cache() const
{
    if (m_cache_built)
        return;

    m_all_paths.clear();
    m_directories.clear();
    m_children.clear();
    m_ordered_paths.clear();

    auto paths = get_all_paths();
    m_ordered_paths = paths;

    // Root is always a directory
    m_directories.insert("");
    m_children[""] = {};

    for (const auto& path : paths)
    {
        m_all_paths.insert(path);

        // Check if it's a directory (ends with /)
        bool is_dir = !path.empty() && path.back() == '/';
        std::string normalized = is_dir ? path.substr(0, path.size() - 1) : path;

        if (is_dir)
            m_directories.insert(normalized);

        // Build parent chain
        std::string current = normalized;
        while (!current.empty())
        {
            size_t last_slash = current.rfind('/');
            std::string parent = (last_slash == std::string::npos) ? "" : current.substr(0, last_slash);
            std::string child_name = (last_slash == std::string::npos) ? current : current.substr(last_slash + 1);

            // Add parent as directory
            m_directories.insert(parent);

            // Add to parent's children (if not already)
            auto& children = m_children[parent];
            if (std::find(children.begin(), children.end(), child_name) == children.end())
                children.push_back(child_name);

            current = parent;
        }
    }

    m_cache_built = true;
}

bool SnapshotReader::exists(std::string_view path) const
{
    build_path_cache();

    std::string path_str{path};
    // Check direct match
    if (m_all_paths.count(path_str))
        return true;

    // Check as directory (with trailing /)
    if (m_all_paths.count(path_str + "/"))
        return true;

    // Check if it's a synthetic directory (parent of other entries)
    return m_directories.count(path_str) > 0;
}

bool SnapshotReader::is_directory(std::string_view path) const
{
    build_path_cache();
    // Normalize: strip trailing slash for lookup
    std::string normalized{path};
    if (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();
    return m_directories.count(normalized) > 0;
}

std::vector<std::string> SnapshotReader::list_dir(std::string_view path) const
{
    build_path_cache();

    // Normalize: strip trailing slash for lookup
    std::string normalized{path};
    if (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    auto it = m_children.find(normalized);
    if (it != m_children.end())
        return it->second;
    return {};
}

std::string SnapshotReader::read_text(std::string_view path) const
{
    auto data = read_binary(path);
    if (data.empty())
        return {};

    // Auto-detect encoding via BOM
    if (data.size() >= 3 &&
        data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
    {
        // UTF-8 with BOM - skip BOM
        return std::string(reinterpret_cast<const char*>(data.data() + 3), data.size() - 3);
    }
    else if (data.size() >= 2 &&
             data[0] == 0xFF && data[1] == 0xFE)
    {
        // UTF-16LE with BOM - convert to UTF-8
        std::wstring_view wide(reinterpret_cast<const wchar_t*>(data.data() + 2),
                               (data.size() - 2) / sizeof(wchar_t));
        return pnq::string::encode_as_utf8(wide);
    }

    // No BOM - assume UTF-8
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

bool SnapshotReader::extract_directory_recursive(std::string_view archive_prefix, std::string_view dest_dir) const
{
    build_path_cache();

    std::string prefix{archive_prefix};
    // Normalize: ensure no trailing slash for comparison
    if (!prefix.empty() && prefix.back() == '/')
        prefix.pop_back();

    std::error_code ec;

    // Create destination directory
    std::filesystem::create_directories(std::filesystem::path{dest_dir}, ec);
    if (ec)
    {
        spdlog::error("Failed to create directory {}: {}", dest_dir, ec.message());
        return false;
    }

    // Iterate all paths, extract those under prefix
    for (const auto& path : m_ordered_paths)
    {
        // Check if path starts with prefix
        if (!path.starts_with(prefix))
            continue;

        // Get relative path (skip prefix + /)
        std::string relative;
        if (path.length() > prefix.length())
        {
            if (path[prefix.length()] == '/')
                relative = path.substr(prefix.length() + 1);
            else
                continue; // Not a child, just similar prefix
        }
        else
        {
            continue; // Exact match of prefix itself
        }

        if (relative.empty())
            continue;

        // Build destination path
        std::filesystem::path dest_path = std::filesystem::path{dest_dir} / relative;

        // Check if it's a directory (ends with /)
        bool is_dir = !path.empty() && path.back() == '/';

        if (is_dir)
        {
            // Create directory
            std::filesystem::create_directories(dest_path, ec);
            if (ec)
            {
                spdlog::error("Failed to create directory {}: {}", dest_path.string(), ec.message());
                return false;
            }
        }
        else
        {
            // Extract file
            if (!extract_to_file(path, dest_path.string()))
            {
                spdlog::error("Failed to extract {}", path);
                return false;
            }
        }
    }

    return true;
}

std::vector<ArchiveEntry> SnapshotReader::entries() const
{
    build_path_cache();

    std::vector<ArchiveEntry> result;
    result.reserve(m_ordered_paths.size());

    for (const auto& path : m_ordered_paths)
    {
        bool is_dir = !path.empty() && path.back() == '/';
        std::string normalized = is_dir ? path.substr(0, path.size() - 1) : path;
        result.push_back({normalized, is_dir});
    }
    return result;
}

// Iterator implementation
SnapshotReader::Iterator::Iterator(const SnapshotReader* reader, size_t index)
    : m_reader{reader}
    , m_index{index}
{
}

ArchiveEntry SnapshotReader::Iterator::operator*() const
{
    m_reader->build_path_cache();
    const auto& path = m_reader->m_ordered_paths[m_index];
    bool is_dir = !path.empty() && path.back() == '/';
    std::string normalized = is_dir ? path.substr(0, path.size() - 1) : path;
    return {normalized, is_dir};
}

SnapshotReader::Iterator& SnapshotReader::Iterator::operator++()
{
    ++m_index;
    return *this;
}

SnapshotReader::Iterator SnapshotReader::Iterator::operator++(int)
{
    Iterator tmp = *this;
    ++m_index;
    return tmp;
}

bool SnapshotReader::Iterator::operator==(const Iterator& other) const
{
    return m_reader == other.m_reader && m_index == other.m_index;
}

bool SnapshotReader::Iterator::operator!=(const Iterator& other) const
{
    return !(*this == other);
}

SnapshotReader::Iterator SnapshotReader::begin() const
{
    build_path_cache();
    return Iterator(this, 0);
}

SnapshotReader::Iterator SnapshotReader::end() const
{
    build_path_cache();
    return Iterator(this, m_ordered_paths.size());
}

size_t SnapshotReader::size() const
{
    build_path_cache();
    return m_ordered_paths.size();
}

} // namespace insti
