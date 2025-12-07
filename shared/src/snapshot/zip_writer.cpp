#include "pch.h"
#include <insti/snapshot/zip_writer.h>

namespace insti
{

ZipSnapshotWriter::ZipSnapshotWriter()
    : m_zip{new mz_zip_archive{}}
    , m_open{false}
    , m_compression_level{COMPRESSION_FAST}
{
}

ZipSnapshotWriter::~ZipSnapshotWriter()
{
    if (m_open)
    {
        mz_zip_writer_finalize_archive(static_cast<mz_zip_archive*>(m_zip));
        mz_zip_writer_end(static_cast<mz_zip_archive*>(m_zip));
    }
    delete static_cast<mz_zip_archive*>(m_zip);
}

bool ZipSnapshotWriter::create(std::string_view path)
{
    close();

    auto* zip = static_cast<mz_zip_archive*>(m_zip);
    memset(zip, 0, sizeof(mz_zip_archive));

    m_path = std::string{path};
    if (!mz_zip_writer_init_file(zip, m_path.c_str(), 0))
    {
        spdlog::error("Failed to create zip: {}", path);
        return false;
    }

    m_open = true;
    return true;
}

void ZipSnapshotWriter::close()
{
    if (m_open)
    {
        mz_zip_writer_finalize_archive(static_cast<mz_zip_archive*>(m_zip));
        mz_zip_writer_end(static_cast<mz_zip_archive*>(m_zip));
        m_open = false;
    }
}

std::string ZipSnapshotWriter::normalize_path(std::string_view path) const
{
    std::string result{path};
    for (char& c : result)
        if (c == '\\') c = '/';
    return result;
}

bool ZipSnapshotWriter::create_directory(std::string_view path)
{
    if (!m_open)
        return false;

    std::string normalized = normalize_path(path);
    if (!normalized.empty() && normalized.back() != '/')
        normalized += '/';

    // Add empty directory entry
    if (!mz_zip_writer_add_mem(
            static_cast<mz_zip_archive*>(m_zip),
            normalized.c_str(),
            nullptr, 0,
            static_cast<mz_uint>(MZ_NO_COMPRESSION)))
    {
        spdlog::error("Failed to create directory in zip: {}", path);
        return false;
    }
    return true;
}

bool ZipSnapshotWriter::write_binary(std::string_view path, const std::vector<uint8_t>& data)
{
    if (!m_open)
        return false;

    std::string normalized = normalize_path(path);

    if (!mz_zip_writer_add_mem(
            static_cast<mz_zip_archive*>(m_zip),
            normalized.c_str(),
            data.data(), data.size(),
            static_cast<mz_uint>(m_compression_level)))
    {
        spdlog::error("Failed to write to zip: {}", path);
        return false;
    }
    return true;
}

bool ZipSnapshotWriter::write_file(std::string_view archive_path, std::string_view src_path)
{
    if (!m_open)
        return false;

    std::string normalized = normalize_path(archive_path);
    std::string src_str{src_path};

    if (!mz_zip_writer_add_file(
            static_cast<mz_zip_archive*>(m_zip),
            normalized.c_str(),
            src_str.c_str(),
            nullptr, 0,
            static_cast<mz_uint>(m_compression_level)))
    {
        spdlog::error("Failed to add file to zip: {} -> {}", src_path, archive_path);
        return false;
    }
    return true;
}

bool ZipSnapshotWriter::finalize()
{
    if (!m_open)
        return false;

    auto* zip = static_cast<mz_zip_archive*>(m_zip);

    if (!mz_zip_writer_finalize_archive(zip))
    {
        spdlog::error("Failed to finalize zip archive");
        mz_zip_writer_end(zip);
        m_open = false;
        return false;
    }

    if (!mz_zip_writer_end(zip))
    {
        spdlog::error("Failed to close zip archive");
        m_open = false;
        return false;
    }

    m_open = false;
    return true;
}

} // namespace insti
