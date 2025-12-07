#pragma once

// Windows
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>

// STL
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <unordered_map>
#include <span>
#include <sstream>

// Third-party
#include <pugixml.hpp>
#include <sqlite3.h>
#include <pnq/pnq.h>
#include <pnq/regis3.h>
#include <pnq/sqlite/sqlite.h>
#include <miniz.h>
