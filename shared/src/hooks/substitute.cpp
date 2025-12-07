#include "pch.h"
#include <insti/hooks/substitute.h>
#include <algorithm>
#include <filesystem>

namespace insti
{

namespace
{

/// Case-insensitive character comparison
bool char_equal_nocase(char a, char b)
{
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

/// Simple glob pattern matching (supports * and ?)
bool matches_glob(std::string_view pattern, std::string_view text)
{
    size_t p = 0, t = 0;
    size_t star_p = std::string_view::npos;
    size_t star_t = 0;

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || char_equal_nocase(pattern[p], text[t])))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star_p = p++;
            star_t = t;
        }
        else if (star_p != std::string_view::npos)
        {
            p = star_p + 1;
            t = ++star_t;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*')
        ++p;

    return p == pattern.size();
}

} // namespace

std::vector<std::string> SubstituteHook::expand_glob(const std::string& resolved_pattern) const
{
    std::vector<std::string> results;

    // Check if pattern contains wildcards
    bool has_wildcard = resolved_pattern.find('*') != std::string::npos ||
                        resolved_pattern.find('?') != std::string::npos;

    if (!has_wildcard)
    {
        // No wildcards - just return the path if it exists
        if (std::filesystem::exists(resolved_pattern))
            results.push_back(resolved_pattern);
        else
            spdlog::warn("File not found: {}", resolved_pattern);
        return results;
    }

    // Split into directory and filename pattern
    std::filesystem::path pattern_path{resolved_pattern};
    std::filesystem::path parent = pattern_path.parent_path();
    std::string filename_pattern = pattern_path.filename().string();

    // Check if directory part also has wildcards (not supported yet)
    if (parent.string().find('*') != std::string::npos ||
        parent.string().find('?') != std::string::npos)
    {
        spdlog::warn("Wildcards in directory path not supported: {}", resolved_pattern);
        return results;
    }

    // Iterate directory and match
    std::error_code ec;
    if (!std::filesystem::exists(parent, ec))
    {
        spdlog::warn("Directory not found: {}", parent.string());
        return results;
    }

    for (const auto& entry : std::filesystem::directory_iterator(parent, ec))
    {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        if (matches_glob(filename_pattern, filename))
        {
            results.push_back(entry.path().string());
        }
    }

    if (results.empty())
        spdlog::warn("No files matched pattern: {}", resolved_pattern);

    return results;
}

bool SubstituteHook::substitute_to_placeholders(
    const std::string& file_path,
    const std::unordered_map<std::string, std::string>& variables) const
{
    // Read file content
    std::string content = pnq::text_file::read_auto(file_path);
    if (content.empty() && !pnq::file::exists(file_path))
    {
        spdlog::error("Failed to read file: {}", file_path);
        return false;
    }

    // Build list of (value, varname) pairs, sorted by value length descending
    // This ensures longest match first to avoid partial substitution
    std::vector<std::pair<std::string, std::string>> replacements;
    for (const auto& [name, value] : variables)
    {
        // Skip empty values and variables that look like placeholders themselves
        if (value.empty() || value.find("${") != std::string::npos)
            continue;

        replacements.emplace_back(value, name);
    }

    // Sort by value length descending
    std::sort(replacements.begin(), replacements.end(),
              [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

    // Perform replacements (case-insensitive for paths)
    std::string result = content;
    bool modified = false;

    for (const auto& [value, name] : replacements)
    {
        std::string placeholder = "${" + name + "}";
        size_t pos = 0;

        while (true)
        {
            // Case-insensitive search
            auto it = std::search(result.begin() + pos, result.end(),
                                  value.begin(), value.end(),
                                  [](char a, char b) {
                                      return std::tolower(static_cast<unsigned char>(a)) ==
                                             std::tolower(static_cast<unsigned char>(b));
                                  });

            if (it == result.end())
                break;

            size_t found_pos = it - result.begin();
            result.replace(found_pos, value.size(), placeholder);
            pos = found_pos + placeholder.size();
            modified = true;
        }
    }

    if (!modified)
    {
        spdlog::debug("No substitutions made in: {}", file_path);
        return true;
    }

    // Write back (no BOM to preserve original format)
    if (!pnq::text_file::write_utf8(file_path, result, false))
    {
        spdlog::error("Failed to write file: {}", file_path);
        return false;
    }

    spdlog::info("Substituted values with placeholders in: {}", file_path);
    return true;
}

bool SubstituteHook::substitute_from_placeholders(
    const std::string& file_path,
    const std::unordered_map<std::string, std::string>& variables) const
{
    // Read file content
    std::string content = pnq::text_file::read_auto(file_path);
    if (content.empty() && !pnq::file::exists(file_path))
    {
        spdlog::error("Failed to read file: {}", file_path);
        return false;
    }

    // Use pnq::string::Expander for ${VAR} replacement
    pnq::string::Expander expander{variables, true};
    expander.expand_dollar(true).expand_percent(true);

    std::string result = expander.expand(content);

    if (result == content)
    {
        spdlog::debug("No placeholders found in: {}", file_path);
        return true;
    }

    // Write back (no BOM to preserve original format)
    if (!pnq::text_file::write_utf8(file_path, result, false))
    {
        spdlog::error("Failed to write file: {}", file_path);
        return false;
    }

    spdlog::info("Resolved placeholders in: {}", file_path);
    return true;
}

bool SubstituteHook::execute(const std::unordered_map<std::string, std::string>& variables) const
{
    // Resolve the file pattern
    pnq::string::Expander expander{variables, true};
    expander.expand_dollar(true).expand_percent(true);
    std::string resolved_pattern = expander.expand(m_file_pattern);

    spdlog::debug("SubstituteHook: pattern '{}' -> '{}'", m_file_pattern, resolved_pattern);

    // Expand glob to file list
    std::vector<std::string> files = expand_glob(resolved_pattern);
    if (files.empty())
        return true; // No files to process - not an error

    bool all_ok = true;
    for (const auto& file : files)
    {
        bool ok = false;

        switch (m_phase)
        {
        case Phase::PreBackup:
            // Replace values with placeholders
            ok = substitute_to_placeholders(file, variables);
            break;

        case Phase::PostRestore:
            // Replace placeholders with values
            ok = substitute_from_placeholders(file, variables);
            break;

        default:
            spdlog::warn("SubstituteHook: unexpected phase {} for file {}",
                         static_cast<int>(m_phase), file);
            ok = true; // Don't fail, just warn
            break;
        }

        if (!ok)
            all_ok = false;
    }

    return all_ok;
}

} // namespace insti
