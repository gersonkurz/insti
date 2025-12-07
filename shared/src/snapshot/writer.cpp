#include "pch.h"
#include <insti/snapshot/writer.h>

namespace insti
{

bool SnapshotWriter::write_text(std::string_view path, std::string_view content)
{
    std::vector<uint8_t> data(content.begin(), content.end());
    return write_binary(path, data);
}

bool SnapshotWriter::write_utf16(std::string_view path, std::string_view content)
{
    // Convert UTF-8 to UTF-16LE
    std::wstring wide = pnq::string::encode_as_utf16(content);

    // Build output with BOM
    std::vector<uint8_t> data;
    data.reserve(2 + wide.size() * sizeof(wchar_t));

    // UTF-16LE BOM
    data.push_back(0xFF);
    data.push_back(0xFE);

    // Append UTF-16LE content
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(wide.data());
    data.insert(data.end(), bytes, bytes + wide.size() * sizeof(wchar_t));

    return write_binary(path, data);
}

bool SnapshotWriter::add_directory_recursive(std::string_view archive_prefix, std::string_view src_dir)
{
    if (!is_open())
        return false;

    std::filesystem::path base{src_dir};
    if (!std::filesystem::exists(base))
    {
        spdlog::error("Source directory does not exist: {}", src_dir);
        return false;
    }

    // Normalize prefix: remove trailing slash if present
    std::string prefix{archive_prefix};
    if (!prefix.empty() && prefix.back() == '/')
        prefix.pop_back();

    std::error_code ec;

    // First pass: collect all directories and files
    std::vector<std::filesystem::path> dirs;
    std::vector<std::filesystem::path> files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec))
    {
        if (ec)
        {
            spdlog::error("Error iterating directory: {}", ec.message());
            return false;
        }

        if (entry.is_directory())
            dirs.push_back(entry.path());
        else if (entry.is_regular_file())
            files.push_back(entry.path());
    }

    // Create empty directories (those without files)
    for (const auto& dir : dirs)
    {
        auto rel = std::filesystem::relative(dir, base, ec);
        if (ec)
        {
            spdlog::error("Error computing relative path: {}", ec.message());
            return false;
        }

        // Check if directory is empty (no files under it)
        bool has_files = false;
        for (const auto& file : files)
        {
            auto file_rel = std::filesystem::relative(file, base, ec);
            std::string file_rel_str = file_rel.string();
            std::string dir_rel_str = rel.string();

            // Replace backslashes for comparison
            std::replace(file_rel_str.begin(), file_rel_str.end(), '\\', '/');
            std::replace(dir_rel_str.begin(), dir_rel_str.end(), '\\', '/');

            if (file_rel_str.starts_with(dir_rel_str + "/"))
            {
                has_files = true;
                break;
            }
        }

        if (!has_files)
        {
            std::string rel_str = rel.string();
            std::replace(rel_str.begin(), rel_str.end(), '\\', '/');
            std::string archive_path = prefix + "/" + rel_str;
            if (!create_directory(archive_path))
                return false;
        }
    }

    // Add all files
    for (const auto& file : files)
    {
        auto rel = std::filesystem::relative(file, base, ec);
        if (ec)
        {
            spdlog::error("Error computing relative path: {}", ec.message());
            return false;
        }

        std::string rel_str = rel.string();
        std::replace(rel_str.begin(), rel_str.end(), '\\', '/');
        std::string archive_path = prefix + "/" + rel_str;
        if (!write_file(archive_path, file.string()))
            return false;
    }

    return true;
}

} // namespace insti
