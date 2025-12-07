#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <pnq/ref_counted.h>

namespace insti
{

/// Abstract base class for writing snapshots.
/// @note Cannot be final - has virtual destructor for polymorphism.
class SnapshotWriter : public pnq::RefCountImpl
{
public:
    virtual ~SnapshotWriter() = default;

    // --- Pure virtual (implementations provide) ---

    /// Create an empty directory entry.
    /// @param path Path within archive (using / separator)
    virtual bool create_directory(std::string_view path) = 0;

    /// Write binary data to archive.
    /// @param path Path within archive (using / separator)
    /// @param data Binary content to write
    virtual bool write_binary(std::string_view path, const std::vector<uint8_t>& data) = 0;

    /// Write file from disk to archive (efficient).
    /// @param archive_path Path within archive (using / separator)
    /// @param src_path Source file path on disk
    virtual bool write_file(std::string_view archive_path, std::string_view src_path) = 0;

    /// Finalize the archive (write central directory).
    virtual bool finalize() = 0;

    /// Close the archive and release resources.
    virtual void close() = 0;

    /// Check if archive is open for writing.
    virtual bool is_open() const = 0;

    // --- ABC provides ---

    /// Write text content to archive (as UTF-8 bytes).
    /// @param path Path within archive (using / separator)
    /// @param content Text content to write
    bool write_text(std::string_view path, std::string_view content);

    /// Write text content to archive as UTF-16LE with BOM.
    /// Used for .reg files which require UTF-16LE encoding.
    /// @param path Path within archive (using / separator)
    /// @param content UTF-8 text content (will be converted to UTF-16LE)
    bool write_utf16(std::string_view path, std::string_view content);

    /// Add a directory recursively from disk.
    /// @param archive_prefix Prefix in archive (e.g. "files/myapp")
    /// @param src_dir Source directory on disk
    bool add_directory_recursive(std::string_view archive_prefix, std::string_view src_dir);
};

} // namespace insti
