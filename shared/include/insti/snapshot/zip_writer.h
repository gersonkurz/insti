#pragma once

#include "writer.h"
#include <pnq/pnq.h>

namespace insti
{

/// Zip implementation of SnapshotWriter using miniz.
class ZipSnapshotWriter final : public SnapshotWriter
{
    PNQ_DECLARE_NON_COPYABLE(ZipSnapshotWriter)

public:
    /// Compression level constants (0-9, or -1 for default).
    static constexpr int COMPRESSION_NONE = 0;
    static constexpr int COMPRESSION_FAST = 1;
    static constexpr int COMPRESSION_BEST = 9;
    static constexpr int COMPRESSION_DEFAULT = -1;  // Usually level 6

    ZipSnapshotWriter();
    ~ZipSnapshotWriter() override;

    /// Create a new zip file for writing.
    /// @param path Path to the zip file on disk
    bool create(std::string_view path);

    /// Set compression level (0-9, or COMPRESSION_DEFAULT).
    /// Must be called before adding files. Default is COMPRESSION_FAST (1).
    void set_compression_level(int level) { m_compression_level = level; }

    // SnapshotWriter implementation
    bool create_directory(std::string_view path) override;
    bool write_binary(std::string_view path, const std::vector<uint8_t>& data) override;
    bool write_file(std::string_view archive_path, std::string_view src_path) override;
    bool finalize() override;
    void close() override;
    bool is_open() const override { return m_open; }

private:
    /// Normalize path separators to forward slashes.
    std::string normalize_path(std::string_view path) const;

    void* m_zip;              ///< miniz archive handle (mz_zip_archive*)
    bool m_open;              ///< Whether archive is currently open
    std::string m_path;       ///< Path to the archive file on disk
    int m_compression_level;  ///< Compression level (default: COMPRESSION_FAST)
};

} // namespace insti
