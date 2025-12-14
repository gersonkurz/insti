#pragma once

#include <string_view>

namespace insti
{

/// Lifecycle stages for hooks.
enum class Lifecycle
{
    Startup,
    Shutdown
};

/// Convert lifecycle to string.
inline const char* lifecycle_to_string(Lifecycle lc)
{
    switch (lc)
    {
        case Lifecycle::Startup:  return "startup";
        case Lifecycle::Shutdown: return "shutdown";
    }
    return "unknown";
}

/// Direction of data flow for transformation hooks.
/// Used by substitute/sql hooks to know whether to resolve or unresolve values.
enum class Direction
{
    Backup,  ///< Values -> Placeholders (unresolve)
    Restore  ///< Placeholders -> Values (resolve)
};

// Legacy alias for code still using Phase
using Phase = Direction;

} // namespace insti
