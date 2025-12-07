#pragma once

#include <string_view>

namespace insti
{

/// Execution phases for hooks.
enum class Phase
{
    PreBackup,
    PostBackup,
    PreRestore,
    PostRestore,
    PreClean,
    PostClean
};

/// Convert phase to string.
inline const char* phase_to_string(Phase phase)
{
    switch (phase)
    {
        case Phase::PreBackup:   return "PreBackup";
        case Phase::PostBackup:  return "PostBackup";
        case Phase::PreRestore:  return "PreRestore";
        case Phase::PostRestore: return "PostRestore";
        case Phase::PreClean:    return "PreClean";
        case Phase::PostClean:   return "PostClean";
    }
    return "Unknown";
}

/// Parse phase from string (case-insensitive).
/// @return true if valid, false otherwise
bool parse_phase(std::string_view str, Phase& out_phase);

} // namespace insti
