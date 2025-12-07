#pragma once

#include <string>

namespace insti
{

/// Entry info from a snapshot archive.
struct ArchiveEntry
{
    std::string path;       ///< Path within archive (using / separator)
    bool is_directory;      ///< True if this is a directory entry
};

} // namespace insti
