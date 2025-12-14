#pragma once

// =============================================================================
// settings.h - Application settings for instinctiv
// =============================================================================
// Uses the shared settings from the library

#include <insti/config/settings.h>

namespace instinctiv
{
namespace config
{

// Re-export from insti::config for backward compatibility
using Settings = insti::config::Settings;
inline Settings& theSettings = insti::config::theSettings;

} // namespace config
} // namespace instinctiv
