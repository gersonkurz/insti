#include "pch.h"
#include <insti/insti.h>
#include <insti/core/project_blueprint.h>

namespace insti
{

Blueprint::~Blueprint()
{
    // RefCountedVector handles releasing actions and hooks
}

void Blueprint::populate_builtins()
{
    m_builtin_variables["PROGRAMFILES"] = pnq::path::get_known_folder(FOLDERID_ProgramFiles).string();
    m_builtin_variables["PROGRAMFILES_X86"] = pnq::path::get_known_folder(FOLDERID_ProgramFilesX86).string();
    m_builtin_variables["PROGRAMDATA"] = pnq::path::get_known_folder(FOLDERID_ProgramData).string();
    m_builtin_variables["APPDATA"] = pnq::path::get_known_folder(FOLDERID_RoamingAppData).string();
    m_builtin_variables["LOCALAPPDATA"] = pnq::path::get_known_folder(FOLDERID_LocalAppData).string();
    m_builtin_variables["WINDIR"] = pnq::directory::windows();

    // COMPUTERNAME
    wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = std::size(computer_name);
    if (GetComputerNameW(computer_name, &size))
        m_builtin_variables["COMPUTERNAME"] = pnq::string::encode_as_utf8(computer_name);

    // USERNAME
    wchar_t user_name[256];
    size = std::size(user_name);
    if (GetUserNameW(user_name, &size))
        m_builtin_variables["USERNAME"] = pnq::string::encode_as_utf8(user_name);

    // SYSTEMDRIVE - from environment
    std::string systemdrive;
    if (pnq::environment_variables::get("SYSTEMDRIVE", systemdrive))
        m_builtin_variables["SYSTEMDRIVE"] = systemdrive;
}

bool Blueprint::resolve_user_variables()
{
    // Build dependency graph and resolve in topological order
    // Simple approach: iterate until no changes, detect cycles by iteration count

    m_resolved_variables = m_builtin_variables;

    // Add unresolved user vars
    for (const auto& [name, value] : m_user_variables)
        m_resolved_variables[name] = value;

    // Resolve iteratively (max iterations = number of user vars + 1)
    const size_t max_iterations = m_user_variables.size() + 1;

    for (size_t i = 0; i < max_iterations; ++i)
    {
        bool changed = false;

        for (const auto& [name, _] : m_user_variables)
        {
            const std::string& current = m_resolved_variables[name];
            std::string resolved = pnq::string::Expander{m_resolved_variables, false}
                .expand_dollar(true)
                .expand_percent(true)
                .expand(current);

            if (resolved != current)
            {
                m_resolved_variables[name] = resolved;
                changed = true;
            }
        }

        if (!changed)
            return true; // All resolved
    }

    // If we get here, there's likely a cycle
    // Find which variables still have unresolved references
    for (const auto& [name, value] : m_user_variables)
    {
        const std::string& current = m_resolved_variables[name];
        if (current.find("${") != std::string::npos || current.find('%') != std::string::npos)
        {
            spdlog::error("Circular dependency or unresolved variable in '{}'", name);
            return false;
        }
    }

    return true;
}

std::string Blueprint::resolve(std::string_view input) const
{
    // Only expand ${VAR} syntax, not %VAR%
    // %VAR% in registry files are runtime variables (e.g., %SystemRoot%) that
    // Windows expands at runtime - we should not touch those
    return pnq::string::Expander{m_resolved_variables, true}
        .expand_dollar(true)
        .expand_percent(false)
        .expand(input);
}

std::string Blueprint::unresolve(std::string_view input) const
{
    // Build sorted list of variables by value length (longest first)
    // This ensures we replace "C:\Program Files (x86)" before "C:\Program Files"
    std::vector<std::pair<std::string, std::string>> sorted_vars;
    sorted_vars.reserve(m_resolved_variables.size());

    for (const auto& [name, value] : m_resolved_variables)
    {
        // Skip empty values and project metadata (not useful for portability)
        if (value.empty())
            continue;
        if (name == VAR_PROJECT_NAME || name == VAR_PROJECT_VERSION || name == VAR_PROJECT_DESCRIPTION)
            continue;
        // SYSTEMDRIVE is almost always "C:" - no portability value
        if (name == "SYSTEMDRIVE")
            continue;

        sorted_vars.emplace_back(name, value);
    }

    // Sort by value length descending
    std::sort(sorted_vars.begin(), sorted_vars.end(),
              [](const auto& a, const auto& b) { return a.second.length() > b.second.length(); });

    std::string result{input};

    for (const auto& [name, value] : sorted_vars)
    {
        std::string placeholder = "${" + name + "}";

        // Case-insensitive search and replace
        size_t pos = 0;
        while ((pos = pnq::string::find_nocase(result, value, pos)) != std::string::npos)
        {
            result.replace(pos, value.length(), placeholder);
            pos += placeholder.length();
        }
    }

    return result;
}

const std::string& Blueprint::get_var(std::string_view name) const
{
    static const std::string empty;
    auto it = m_resolved_variables.find(name.data());
    return it != m_resolved_variables.end() ? it->second : empty;
}

void Blueprint::set_override(std::string_view name, std::string_view value)
{
    // TODO: Move to ActionContext once CLI is rewritten (Phase 3)
    std::string resolved = pnq::string::Expander{m_resolved_variables, true}
        .expand_dollar(true)
        .expand_percent(true)
        .expand(value);

    m_resolved_variables[name.data()] = resolved;
}

Blueprint* Blueprint::load_from_file(std::string_view path)
{
    // Delegate to ProjectBlueprint for proper typing
    return ProjectBlueprint::load_from_file(path);
}

Blueprint* Blueprint::load_from_string(std::string_view xml)
{
    // For backward compatibility - creates a base Blueprint
    // New code should use ProjectBlueprint::load_from_file or InstanceBlueprint::load_from_archive
    auto bp = new Blueprint();
    bp->populate_builtins();

    if (!bp->parse_xml(xml))
    {
        bp->release(REFCOUNT_DEBUG_ARGS);
        return nullptr;
    }

    return bp;
}

bool Blueprint::parse_xml(std::string_view xml)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(std::string{xml}.c_str());
    if (!result)
    {
        spdlog::error("XML parse error: {} at offset {}", result.description(), result.offset);
        return false;
    }

    auto root = doc.child("blueprint");
    if (!root)
    {
        spdlog::error("Missing <blueprint> root element");
        return false;
    }

    // Project metadata as builtin variables
    std::string proj_name = root.attribute("name").as_string();
    std::string proj_version = root.attribute("version").as_string();

    if (proj_name.empty())
    {
        spdlog::error("Blueprint missing 'name' attribute");
        return false;
    }
    if (proj_version.empty())
    {
        spdlog::error("Blueprint missing 'version' attribute");
        return false;
    }

    m_builtin_variables[VAR_PROJECT_NAME.data()] = std::move(proj_name);
    m_builtin_variables[VAR_PROJECT_VERSION.data()] = std::move(proj_version);

    // Install directory (optional - can reference other variables)
    std::string installdir = root.attribute("installdir").as_string();
    if (!installdir.empty())
        m_user_variables[VAR_INSTALLDIR.data()] = std::move(installdir);

    // Description (optional)
    if (auto desc = root.child("description"))
        m_builtin_variables[VAR_PROJECT_DESCRIPTION.data()] = desc.text().as_string();

    // Variables
    if (auto vars = root.child("variables"))
    {
        for (auto var : vars.children("var"))
        {
            std::string name = var.attribute("name").as_string();
            if (name.empty())
            {
                spdlog::error("Variable missing 'name' attribute");
                return false;
            }

            std::string value = var.text().as_string();
            m_user_variables[name] = value;
        }
    }

    // Resolve user variables
    if (!resolve_user_variables())
        return false;

    // Actions
    if (auto resources = root.child("resources"))
    {
        for (auto node : resources.children())
        {
            std::string_view node_name = node.name();

            if (node_name == CopyDirectoryAction::TYPE_NAME)
            {
                std::string path = node.attribute("path").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (path.empty())
                {
                    spdlog::error("files resource missing 'path' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("files resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new CopyDirectoryAction(std::move(path), std::move(archive_path)));
            }
            else if (node_name == CopyFileAction::TYPE_NAME)
            {
                std::string path = node.attribute("path").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (path.empty())
                {
                    spdlog::error("file resource missing 'path' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("file resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new CopyFileAction(std::move(path), std::move(archive_path)));
            }
            else if (node_name == RegistryAction::TYPE_NAME)
            {
                std::string key = node.attribute("key").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (key.empty())
                {
                    spdlog::error("registry resource missing 'key' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("registry resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new RegistryAction(std::move(key), std::move(archive_path)));
            }
            else if (node_name == EnvironmentAction::TYPE_NAME)
            {
                std::string name = node.attribute("name").as_string();
                std::string archive_path = node.attribute("archive").as_string();
                std::string scope_str = node.attribute("scope").as_string("user");

                if (name.empty())
                {
                    spdlog::error("environment resource missing 'name' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("environment resource missing 'archive' attribute");
                    return false;
                }

                EnvironmentScope scope;
                if (pnq::string::equals_nocase(scope_str, "user"))
                    scope = EnvironmentScope::User;
                else if (pnq::string::equals_nocase(scope_str, "system"))
                    scope = EnvironmentScope::System;
                else
                {
                    spdlog::error("environment resource invalid 'scope': {} (expected 'user' or 'system')", scope_str);
                    return false;
                }

                m_actions.push_back(new EnvironmentAction(std::move(name), scope, std::move(archive_path)));
            }
            else if (node_name == DelimitedEntryAction::TYPE_NAME)
            {
                std::string key = node.attribute("key").as_string();
                std::string value_name = node.attribute("value").as_string();
                std::string entry = node.attribute("entry").as_string();
                std::string archive_path = node.attribute("archive").as_string();
                std::string delimiter = node.attribute("delimiter").as_string(";");

                if (key.empty())
                {
                    spdlog::error("delimited resource missing 'key' attribute");
                    return false;
                }
                if (value_name.empty())
                {
                    spdlog::error("delimited resource missing 'value' attribute");
                    return false;
                }
                if (entry.empty())
                {
                    spdlog::error("delimited resource missing 'entry' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("delimited resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new DelimitedEntryAction(std::move(key), std::move(value_name),
                                                              std::move(entry), std::move(archive_path),
                                                              std::move(delimiter)));
            }
            else if (node_name == MultiStringEntryAction::TYPE_NAME)
            {
                std::string key = node.attribute("key").as_string();
                std::string value_name = node.attribute("value").as_string();
                std::string entry = node.attribute("entry").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (key.empty())
                {
                    spdlog::error("multistring resource missing 'key' attribute");
                    return false;
                }
                if (value_name.empty())
                {
                    spdlog::error("multistring resource missing 'value' attribute");
                    return false;
                }
                if (entry.empty())
                {
                    spdlog::error("multistring resource missing 'entry' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("multistring resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new MultiStringEntryAction(std::move(key), std::move(value_name),
                                                                std::move(entry), std::move(archive_path)));
            }
            else if (node_name == ServiceAction::TYPE_NAME)
            {
                std::string name = node.attribute("name").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (name.empty())
                {
                    spdlog::error("service resource missing 'name' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("service resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new ServiceAction(std::move(name), std::move(archive_path)));
            }
            else if (node_name == HostsAction::TYPE_NAME)
            {
                std::string hostname = node.attribute("hostname").as_string();
                std::string archive_path = node.attribute("archive").as_string();

                if (hostname.empty())
                {
                    spdlog::error("hosts resource missing 'hostname' attribute");
                    return false;
                }
                if (archive_path.empty())
                {
                    spdlog::error("hosts resource missing 'archive' attribute");
                    return false;
                }

                m_actions.push_back(new HostsAction(std::move(hostname), std::move(archive_path)));
            }
        }
    }

    // Hooks
    if (auto hooks_node = root.child("hooks"))
    {
        for (auto node : hooks_node.children())
        {
            std::string_view node_name = node.name();

            // Parse phase (required for all hooks)
            std::string phase_str = node.attribute("phase").as_string();
            if (phase_str.empty())
            {
                spdlog::error("{} hook missing 'phase' attribute", node_name);
                return false;
            }

            Phase phase;
            if (!parse_phase(phase_str, phase))
            {
                spdlog::error("Invalid phase '{}' for {} hook", phase_str, node_name);
                return false;
            }

            if (node_name == "kill")
            {
                std::string process = node.attribute("process").as_string();
                if (process.empty())
                {
                    spdlog::error("kill hook missing 'process' attribute");
                    return false;
                }

                uint32_t timeout = node.attribute("timeout").as_uint(5000);
                m_hooks[static_cast<size_t>(phase)].push_back(
                    new KillProcessHook(std::move(process), timeout));
            }
            else if (node_name == "run")
            {
                std::string path = node.attribute("path").as_string();
                if (path.empty())
                {
                    spdlog::error("run hook missing 'path' attribute");
                    return false;
                }

                bool wait = node.attribute("wait").as_bool(true);
                bool ignore_exit = node.attribute("ignore-exit-code").as_bool(false);

                // Collect arguments
                std::vector<std::string> args;
                for (auto arg : node.children("arg"))
                {
                    args.push_back(arg.text().as_string());
                }

                m_hooks[static_cast<size_t>(phase)].push_back(
                    new RunProcessHook(std::move(path), std::move(args), wait, ignore_exit));
            }
            else if (node_name == "substitute")
            {
                std::string file = node.attribute("file").as_string();
                if (file.empty())
                {
                    spdlog::error("substitute hook missing 'file' attribute");
                    return false;
                }

                auto* hook = new SubstituteHook(std::move(file));
                hook->set_phase(phase);
                m_hooks[static_cast<size_t>(phase)].push_back(hook);
            }
            else if (node_name == "sql")
            {
                std::string file = node.attribute("file").as_string();
                std::string query = node.attribute("query").as_string();
                if (file.empty())
                {
                    spdlog::error("sql hook missing 'file' attribute");
                    return false;
                }
                if (query.empty())
                {
                    spdlog::error("sql hook missing 'query' attribute");
                    return false;
                }

                auto* hook = new SqlHook(std::move(file), std::move(query));
                hook->set_phase(phase);
                m_hooks[static_cast<size_t>(phase)].push_back(hook);
            }
            else
            {
                spdlog::warn("Unknown hook type: {}", node_name);
            }
        }
    }

    return true;
}

std::string Blueprint::to_xml() const
{
    pugi::xml_document doc;

    auto decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    auto root = doc.append_child("blueprint");
    root.append_attribute("name") = name().c_str();
    root.append_attribute("version") = version().c_str();

    // Write installdir if set (from user_variables, not resolved)
    auto installdir_it = m_user_variables.find(VAR_INSTALLDIR.data());
    if (installdir_it != m_user_variables.end() && !installdir_it->second.empty())
        root.append_attribute("installdir") = installdir_it->second.c_str();

    if (!description().empty())
        root.append_child("description").text().set(description().c_str());

    // Write user variables (excluding INSTALLDIR which is written as attribute)
    bool has_other_vars = false;
    for (const auto& [name, value] : m_user_variables)
    {
        if (name != VAR_INSTALLDIR)
        {
            has_other_vars = true;
            break;
        }
    }

    if (has_other_vars)
    {
        auto vars = root.append_child("variables");
        for (const auto& [name, value] : m_user_variables)
        {
            if (name == VAR_INSTALLDIR)
                continue;  // Already written as attribute

            auto var = vars.append_child("var");
            var.append_attribute("name") = name.c_str();
            var.text().set(value.c_str());
        }
    }

    if (!m_actions.empty())
    {
        auto resources = root.append_child("resources");
        for (const auto* action : m_actions)
        {
            auto node = resources.append_child(action->type_name());
            for (const auto& [key, value] : action->to_params())
            {
                node.append_attribute(key.c_str()) = value.c_str();
            }
        }
    }

    // Hooks
    bool has_hooks = false;
    for (const auto& phase_hooks : m_hooks)
    {
        if (!phase_hooks.empty())
        {
            has_hooks = true;
            break;
        }
    }

    if (has_hooks)
    {
        auto hooks_node = root.append_child("hooks");
        for (size_t i = 0; i < 6; ++i)
        {
            Phase phase = static_cast<Phase>(i);
            for (const auto* hook : m_hooks[i])
            {
                if (auto* kill = dynamic_cast<const KillProcessHook*>(hook))
                {
                    auto node = hooks_node.append_child("kill");
                    node.append_attribute("phase") = phase_to_string(phase);
                    node.append_attribute("process") = kill->process_name().c_str();
                    if (kill->timeout_ms() != 5000)
                        node.append_attribute("timeout") = kill->timeout_ms();
                }
                else if (auto* run = dynamic_cast<const RunProcessHook*>(hook))
                {
                    auto node = hooks_node.append_child("run");
                    node.append_attribute("phase") = phase_to_string(phase);
                    node.append_attribute("path") = run->path().c_str();
                    if (!run->wait())
                        node.append_attribute("wait") = false;
                    if (run->ignore_exit_code())
                        node.append_attribute("ignore-exit-code") = true;

                    for (const auto& arg : run->args())
                    {
                        node.append_child("arg").text().set(arg.c_str());
                    }
                }
                else if (auto* sub = dynamic_cast<const SubstituteHook*>(hook))
                {
                    auto node = hooks_node.append_child("substitute");
                    node.append_attribute("phase") = phase_to_string(phase);
                    node.append_attribute("file") = sub->file_pattern().c_str();
                }
                else if (auto* sql = dynamic_cast<const SqlHook*>(hook))
                {
                    auto node = hooks_node.append_child("sql");
                    node.append_attribute("phase") = phase_to_string(phase);
                    node.append_attribute("file") = sql->file_path().c_str();
                    node.append_attribute("query") = sql->query().c_str();
                }
            }
        }
    }

    std::ostringstream oss;
    doc.save(oss, "    ");
    return oss.str();
}

} // namespace insti
