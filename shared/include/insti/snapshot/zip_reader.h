#pragma once

#include "reader.h"
#include <pnq/pnq.h>

namespace insti
{

/// Zip implementation of SnapshotReader using miniz.
class ZipSnapshotReader final : public SnapshotReader
{
    PNQ_DECLARE_NON_COPYABLE(ZipSnapshotReader)

public:
    ZipSnapshotReader();
    ~ZipSnapshotReader() override;

    /// Open a zip file for reading.
    /// @param path Path to the zip file on disk
    bool open(std::string_view path);

    // SnapshotReader implementation
    std::vector<std::string> get_all_paths() const override;
    std::vector<uint8_t> read_binary(std::string_view path) const override;
    bool extract_to_file(std::string_view archive_path, std::string_view dest_path) const override;
    void close() override;
    bool is_open() const override { return m_open; }

private:
    void* m_zip;  ///< miniz archive handle (mz_zip_archive*)
    bool m_open;  ///< Whether archive is currently open
};

} // namespace insti
