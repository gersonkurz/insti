#include "pch.h"
#include <insti/insti.h>
#include <insti/registry/snapshot_registry.h>
#include <insti/core/orchestrator.h>
#include <pnq/console.h>
#include <pnq/regis3.h>
#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <indicators/progress_bar.hpp>
#include "settings.h"

namespace con = pnq::console;

bool g_verbose = false;

void print_error(const std::string& msg);
void print_verbose(const std::string& msg);

// CLI callback with progress bar
class ProgressBarCallback : public insti::IActionCallback
{
    indicators::ProgressBar m_bar{
        indicators::option::BarWidth{40},
        indicators::option::ShowPercentage{true},
        indicators::option::ShowElapsedTime{true},
        indicators::option::ShowRemainingTime{true},
        indicators::option::PrefixText{""},
        indicators::option::ForegroundColor{indicators::Color::green}
    };
    std::string m_last_phase;
    bool m_completed = false;

public:
    void on_progress(std::string_view phase, std::string_view detail, int percent) override
    {
        if (m_completed) return;

        if (phase != m_last_phase)
        {
            m_last_phase = std::string(phase);
            m_bar.set_option(indicators::option::PrefixText{m_last_phase + " "});
        }

        // Truncate detail if too long
        std::string detail_str(detail);
        if (detail_str.length() > 30)
            detail_str = detail_str.substr(0, 27) + "...";
        m_bar.set_option(indicators::option::PostfixText{detail_str});

        if (percent >= 0)
        {
            m_bar.set_progress(static_cast<size_t>(percent));
            if (percent >= 100)
                m_completed = true;  // Stop further updates
        }
    }

    void on_warning(std::string_view message) override
    {
        // Print warning on new line, then continue
        std::cerr << "\nwarning: " << message << std::endl;
    }

    Decision on_error(std::string_view message, std::string_view context) override
    {
        std::cerr << "\nerror: " << message;
        if (!context.empty())
            std::cerr << " (" << context << ")";
        std::cerr << std::endl;
        return Decision::Abort;
    }

    Decision on_file_conflict(std::string_view path, std::string_view action) override
    {
        // Auto-overwrite for CLI
        return Decision::Continue;
    }

    void complete()
    {
        if (!m_completed)
        {
            m_bar.set_progress(100);  // This also marks as completed when hitting 100%
            m_completed = true;
        }
    }
};

// Unified reference resolution for both projects (A, B, C) and instances (1, 2, 3)
enum class RefType { Project, Instance };

struct ResolvedRef
{
    std::string path;
    RefType type;
    std::string error;

    bool ok() const { return error.empty(); }
    static ResolvedRef fail(const std::string& err) { return {{}, {}, err}; }
    static ResolvedRef project(const std::string& p) { return {p, RefType::Project, {}}; }
    static ResolvedRef instance(const std::string& p) { return {p, RefType::Instance, {}}; }
};

ResolvedRef resolve_reference(const std::string& ref)
{
    // If it's an existing file, use it directly
    if (std::filesystem::exists(ref))
    {
        // Determine type by extension
        if (ref.ends_with(".zip"))
            return ResolvedRef::instance(ref);
        else
            return ResolvedRef::project(ref);
    }

    insti::config::theSettings.load();
    std::string roots_str = insti::config::theSettings.registry.roots.get();
    insti::SnapshotRegistry registry{pnq::string::split(roots_str, ";")};
    registry.initialize();

    // Check if ref is a letter index (A, B, C, ...) -> project
    if (ref.size() == 1 && std::isalpha(ref[0]))
    {
        char c = static_cast<char>(std::toupper(ref[0]));
        size_t index = static_cast<size_t>(c - 'A');

        auto projects = registry.discover_projects("");
        std::sort(projects.begin(), projects.end(), [](const insti::Project* a, const insti::Project* b) {
            return a->project_name() < b->project_name();
        });

        if (index >= projects.size())
        {
            std::string err = "Index " + ref + " out of range (A-" +
                std::string(1, static_cast<char>('A' + projects.size() - 1)) + ")";
            for (auto* p : projects) PNQ_RELEASE(p);
            return ResolvedRef::fail(err);
        }

        std::string path = projects[index]->source_path();
        print_verbose("Resolved " + ref + " to project: " + path);
        for (auto* p : projects) PNQ_RELEASE(p);
        return ResolvedRef::project(path);
    }

    // Check if ref is a numeric index (1, 2, 3, ...) -> instance
    bool is_numeric = !ref.empty() && std::all_of(ref.begin(), ref.end(), ::isdigit);
    if (is_numeric)
    {
        size_t index = std::stoul(ref);
        if (index == 0)
            return ResolvedRef::fail("Index must be >= 1");

        auto entries = registry.discover_instances("");
        std::sort(entries.begin(), entries.end(), [](const insti::Instance* a, const insti::Instance* b) {
            if (a->project_name() != b->project_name())
                return a->project_name() < b->project_name();
            return a->m_timestamp > b->m_timestamp;
        });

        if (index > entries.size())
        {
            std::string err = "Index " + ref + " out of range (1-" + std::to_string(entries.size()) + ")";
            for (auto* e : entries) PNQ_RELEASE(e);
            return ResolvedRef::fail(err);
        }

        std::string path = entries[index - 1]->m_snapshot_path;
        print_verbose("Resolved " + ref + " to snapshot: " + path);
        for (auto* e : entries) PNQ_RELEASE(e);
        return ResolvedRef::instance(path);
    }

    // Try to match by name against instances
    auto matches = registry.discover_instances(ref);
    if (matches.empty())
        return ResolvedRef::fail("Not found: " + ref);

    if (matches.size() > 1)
    {
        std::string err = "Ambiguous reference - multiple matches:";
        for (const auto* entry : matches)
        {
            err += "\n  " + entry->m_snapshot_path;
            PNQ_RELEASE(entry);
        }
        return ResolvedRef::fail(err);
    }

    std::string path = matches[0]->m_snapshot_path;
    print_verbose("Resolved " + ref + " to: " + path);
    PNQ_RELEASE(matches[0]);
    return ResolvedRef::instance(path);
}

void print_error(const std::string& msg)
{
    con::write(CONSOLE_FOREGROUND_RED "error: " CONSOLE_STANDARD);
    con::write_line(msg);
}

void print_verbose(const std::string& msg)
{
    if (g_verbose)
        con::write_line(msg);
}

// Helper to load blueprint from any reference (project or instance)
insti::Blueprint* load_blueprint(const ResolvedRef& resolved)
{
    if (resolved.type == RefType::Instance)
        return insti::Instance::load_from_archive(resolved.path);
    else
        return insti::Project::load_from_file(resolved.path);
}

int cmd_startup(const std::string& source_ref, bool force)
{
    auto resolved = resolve_reference(source_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    auto* bp = load_blueprint(resolved);
    if (!bp)
    {
        print_error("Failed to load blueprint: " + resolved.path);
        return 1;
    }

    con::format_line("Starting: {} v{}", bp->project_name(), bp->project_version());

    const auto& hooks = bp->startup_hooks();
    if (hooks.empty())
    {
        con::write_line("No startup hooks defined.");
        bp->release(REFCOUNT_DEBUG_ARGS);
        return 0;
    }

    const auto& vars = bp->resolved_variables();
    bool success = true;

    for (auto* hook : hooks)
    {
        if (hook->is_force() && !force)
            continue;

        con::format_line("  Running: {}", hook->type_name());
        if (!hook->execute(vars))
        {
            print_error("Hook failed: " + hook->type_name());
            success = false;
            break;
        }
    }

    if (success)
        con::write_line("Startup complete.");

    bp->release(REFCOUNT_DEBUG_ARGS);
    return success ? 0 : 1;
}

int cmd_shutdown(const std::string& source_ref, bool force)
{
    auto resolved = resolve_reference(source_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    auto* bp = load_blueprint(resolved);
    if (!bp)
    {
        print_error("Failed to load blueprint: " + resolved.path);
        return 1;
    }

    con::format_line("Stopping: {} v{}", bp->project_name(), bp->project_version());

    const auto& hooks = bp->shutdown_hooks();
    if (hooks.empty())
    {
        con::write_line("No shutdown hooks defined.");
        bp->release(REFCOUNT_DEBUG_ARGS);
        return 0;
    }

    const auto& vars = bp->resolved_variables();
    bool success = true;

    for (auto* hook : hooks)
    {
        if (hook->is_force() && !force)
            continue;

        con::format_line("  Running: {}", hook->type_name());
        if (!hook->execute(vars))
        {
            // Shutdown failures are warnings, not errors (app might not be running)
            con::format_line("  Warning: {} failed (continuing)", hook->type_name());
        }
    }

    con::write_line("Shutdown complete.");

    bp->release(REFCOUNT_DEBUG_ARGS);
    return 0; // Shutdown always succeeds (best effort)
}

int cmd_backup(const std::string& blueprint_ref, const std::string& output_arg, bool force, const std::string& description)
{
    auto resolved = resolve_reference(blueprint_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    insti::Project* project = nullptr;
    std::string original_snapshot_path;  // For instance updates: path to delete after success

    if (resolved.type == RefType::Instance)
    {
        // Re-backup from existing snapshot - will replace original after success
        original_snapshot_path = resolved.path;

        insti::ZipSnapshotReader reader;
        if (!reader.open(resolved.path))
        {
            print_error("Failed to open snapshot: " + resolved.path);
            return 1;
        }

        std::string blueprint_xml = reader.read_text("blueprint.xml");
        if (blueprint_xml.empty())
        {
            print_error("No blueprint.xml in snapshot");
            return 1;
        }

        project = insti::Project::load_from_string(blueprint_xml, resolved.path);
    }
    else
    {
        project = insti::Project::load_from_file(resolved.path);
    }

    if (!project)
    {
        print_error("Failed to load blueprint: " + resolved.path);
        return 1;
    }

    // Setup registry for path generation
    insti::config::theSettings.load();
    std::string roots_str = insti::config::theSettings.registry.roots.get();
    insti::SnapshotRegistry registry{pnq::string::split(roots_str, ";")};
    registry.initialize();

    std::string output_path = output_arg;

    // Auto-generate output path if not specified
    if (output_path.empty())
    {
        std::string root = registry.first_writable_root();
        if (root.empty())
        {
            print_error("No writable registry root configured. Run instinctiv to configure.");
            project->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }

        std::string filename = registry.generate_filename(
            project->project_name(), std::chrono::system_clock::now());

        output_path = (std::filesystem::path(root) / filename).string();
        print_verbose("Auto-generated path: " + output_path);
    }

    con::format_line("Backing up: {} v{}", project->project_name(), project->project_version());

    // Use orchestrator with progress bar
    ProgressBarCallback callback;
    insti::Orchestrator orc{&registry};

    bool success = orc.backup(project, output_path, &callback, force, description);
    callback.complete();

    if (success)
    {
        con::write_line("");
        con::format_line("Snapshot created: {}", output_path);

        // If this was a re-backup from an existing snapshot, delete the original
        if (!original_snapshot_path.empty())
        {
            std::error_code ec;
            std::filesystem::remove(original_snapshot_path, ec);
            if (ec)
                print_error("Warning: Failed to delete original snapshot: " + original_snapshot_path);
            else
                print_verbose("Deleted original snapshot: " + original_snapshot_path);
        }
    }

    project->release(REFCOUNT_DEBUG_ARGS);
    return success ? 0 : 1;
}

int cmd_restore(const std::string& snapshot_ref, const std::string& dest_override,
                const std::vector<std::string>& var_overrides, bool force)
{
    if (!dest_override.empty())
    {
        print_error("--dest override is not supported. Use variable overrides (--var) instead.");
        return 1;
    }

    auto resolved = resolve_reference(snapshot_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    if (resolved.type == RefType::Project)
    {
        print_error("Cannot restore from a project blueprint. Use a snapshot (1, 2, 3) or .zip file.");
        return 1;
    }

    auto* instance = insti::Instance::load_from_archive(resolved.path);
    if (!instance)
    {
        print_error("Failed to load snapshot: " + resolved.path);
        return 1;
    }

    // Apply variable overrides
    for (const auto& override_str : var_overrides)
    {
        auto pos = override_str.find('=');
        if (pos == std::string::npos)
        {
            print_error("Invalid --var format (expected NAME=VALUE): " + override_str);
            instance->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }
        std::string name = override_str.substr(0, pos);
        std::string value = override_str.substr(pos + 1);
        instance->set_override(name, value);
        print_verbose("  Override: " + name + " = " + value);
    }

    con::format_line("Restoring: {} v{}", instance->project_name(), instance->project_version());

    // Setup registry
    insti::config::theSettings.load();
    std::string roots_str = insti::config::theSettings.registry.roots.get();
    insti::SnapshotRegistry registry{pnq::string::split(roots_str, ";")};
    registry.initialize();

    // Use orchestrator with progress bar
    ProgressBarCallback callback;
    insti::Orchestrator orc{&registry};

    bool success = orc.restore(instance, resolved.path, &callback, false, force);
    callback.complete();

    if (success)
    {
        con::write_line("");
        con::write_line("Restore complete");
    }

    instance->release(REFCOUNT_DEBUG_ARGS);
    return success ? 0 : 1;
}

int cmd_clean(const std::string& source_ref, bool force)
{
    auto resolved = resolve_reference(source_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    insti::Blueprint* bp = nullptr;

    if (resolved.type == RefType::Instance)
    {
        bp = insti::Instance::load_from_archive(resolved.path);
    }
    else
    {
        bp = insti::Project::load_from_file(resolved.path);
    }

    if (!bp)
    {
        print_error("Failed to load blueprint: " + resolved.path);
        return 1;
    }

    con::format_line("Cleaning: {} v{}", bp->project_name(), bp->project_version());

    // Setup registry (needed for orchestrator)
    insti::config::theSettings.load();
    std::string roots_str = insti::config::theSettings.registry.roots.get();
    insti::SnapshotRegistry registry{pnq::string::split(roots_str, ";")};
    registry.initialize();

    // Use orchestrator with progress bar
    ProgressBarCallback callback;
    insti::Orchestrator orc{&registry};

    bool success = orc.clean(bp, &callback, false, force);
    callback.complete();

    if (success)
    {
        con::write_line("");
        con::write_line("Clean complete");
    }

    bp->release(REFCOUNT_DEBUG_ARGS);
    return success ? 0 : 1;
}

int cmd_list_archive(const std::string& snapshot_path)
{
    insti::ZipSnapshotReader reader;
    if (!reader.open(snapshot_path))
    {
        print_error("Failed to open snapshot: " + snapshot_path);
        return 1;
    }

    con::format_line("Snapshot: {} ({} entries)", snapshot_path, reader.size());

    for (const auto& entry : reader)
    {
        if (entry.is_directory)
            con::format_line("  [DIR]  {}", entry.path);
        else
            con::format_line("  [FILE] {}", entry.path);
    }
    return 0;
}

int cmd_list_registry(const std::string& filter_project, bool xml_output)
{
    // Load settings (same config file as instinctiv)
    insti::config::theSettings.load();

    std::string roots_str = insti::config::theSettings.registry.roots.get();
    if (roots_str.empty())
    {
        if (!xml_output)
        {
            con::write_line("No registry roots configured.");
            con::write_line("Run instinctiv to configure snapshot directories.");
        }
        return 0;
    }

    // Parse roots (semicolon-separated)
    auto roots = pnq::string::split(roots_str, ";");

    insti::SnapshotRegistry registry{ roots };
    registry.initialize();

    // Get both projects and instances
    auto projects = registry.discover_projects(filter_project);
    auto entries = registry.discover_instances(filter_project);

    // Sort projects alphabetically by name
    std::sort(projects.begin(), projects.end(), [](const insti::Project* a, const insti::Project* b) {
        return a->project_name() < b->project_name();
    });

    // Sort instances alphabetically by name, then by timestamp (newest first within same name)
    std::sort(entries.begin(), entries.end(), [](const insti::Instance* a, const insti::Instance* b) {
        if (a->project_name() != b->project_name())
            return a->project_name() < b->project_name();
        return a->m_timestamp > b->m_timestamp;
    });

    if (projects.empty() && entries.empty())
    {
        if (!xml_output)
        {
            con::write_line("No blueprints or snapshots found.");
            if (g_verbose)
            {
                con::write_line("");
                con::write_line("Searched in:");
                for (const auto& root : roots)
                    con::format_line("  {}", root);
            }
        }
        return 0;
    }

    if (xml_output)
    {
        // Dump blueprint XML from each snapshot
        for (const auto* e : entries)
        {
            insti::ZipSnapshotReader reader;
            if (reader.open(e->m_snapshot_path))
            {
                std::string blueprint_xml = reader.read_text("blueprint.xml");
                if (!blueprint_xml.empty())
                {
                    con::format_line("<!-- {} -->", e->m_snapshot_path);
                    con::write_line(blueprint_xml);
                    con::write_line("");
                }
            }
        }
    }
    else
    {
        // === PROJECT BLUEPRINTS TABLE ===
        if (!projects.empty())
        {
            // Calculate column widths for projects
            size_t w_name = 4, w_version = 7, w_desc = 11;
            for (const auto* p : projects)
            {
                w_name = std::max(w_name, p->project_name().size());
                w_version = std::max(w_version, p->project_version().size());
                w_desc = std::max(w_desc, std::min(p->project_description().size(), size_t(40)));
            }

            con::write_line("Project Blueprints (use 'insti backup <#>' to create snapshot):");
            con::write_line("");

            // Header
            con::format("{:>2}  ", "#");
            con::format("{:<{}}  ", "Name", w_name);
            con::format("{:<{}}  ", "Version", w_version);
            con::format("{:<{}}", "Description", w_desc);
            con::write_line("");

            // Separator
            con::write_line(std::string(2 + w_name + w_version + w_desc + 6, '-'));

            // Rows (A, B, C, ...)
            for (size_t i = 0; i < projects.size(); ++i)
            {
                const auto* p = projects[i];
                char idx_char = 'A' + static_cast<char>(i);
                std::string idx_str(1, idx_char);
                con::format("{:>2}  ", idx_str);
                con::format("{:<{}}  ", p->project_name(), w_name);
                con::format("{:<{}}  ", p->project_version(), w_version);

                std::string desc = p->project_description();
                if (desc.size() > 40) desc = desc.substr(0, 37) + "...";
                con::format("{:<{}}", desc, w_desc);
                con::write_line("");

                if (g_verbose)
                    con::format_line("     -> {}", p->source_path());
            }
            con::write_line("");
        }

        // === INSTANCE SNAPSHOTS TABLE ===
        if (!entries.empty())
        {
            // Calculate column widths
            size_t w_idx = std::to_string(entries.size()).size() + 1;  // +1 for potential *
            w_idx = std::max(w_idx, size_t(2));
            size_t w_name = 4, w_version = 7, w_timestamp = 9, w_desc = 11, w_machine = 7, w_user = 4;
            for (const auto* e : entries)
            {
                w_name = std::max(w_name, e->project_name().size());
                w_version = std::max(w_version, e->project_version().size());
                w_timestamp = std::max(w_timestamp, e->timestamp_string().size());
                w_desc = std::max(w_desc, std::min(e->m_description.size(), size_t(30)));
                w_machine = std::max(w_machine, e->m_machine.size());
                w_user = std::max(w_user, e->m_user.size());
            }

            con::write_line("Snapshots (use 'insti restore <#>' to restore, * = installed):");
            con::write_line("");

            // Header
            con::format("{:>{}}  ", "#", w_idx);
            con::format("{:<{}}  ", "Name", w_name);
            con::format("{:<{}}  ", "Version", w_version);
            con::format("{:<{}}  ", "Timestamp", w_timestamp);
            con::format("{:<{}}  ", "Description", w_desc);
            con::format("{:<{}}  ", "Machine", w_machine);
            con::format("{:<{}}", "User", w_user);
            con::write_line("");

            // Separator
            con::write_line(std::string(w_idx + w_name + w_version + w_timestamp + w_desc + w_machine + w_user + 12, '-'));

            // Rows
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto* e = entries[i];
                bool is_installed = (e->m_install_status == insti::InstallStatus::Installed);
                std::string idx_str = std::to_string(i + 1) + (is_installed ? "*" : "");
                con::format("{:>{}}  ", idx_str, w_idx);
                con::format("{:<{}}  ", e->project_name(), w_name);
                con::format("{:<{}}  ", e->project_version(), w_version);
                con::format("{:<{}}  ", e->timestamp_string(), w_timestamp);

                // Truncate description if too long
                std::string desc = e->m_description;
                if (desc.size() > 30) desc = desc.substr(0, 27) + "...";
                con::format("{:<{}}  ", desc, w_desc);

                con::format("{:<{}}  ", e->m_machine, w_machine);
                con::format("{:<{}}", e->m_user, w_user);
                con::write_line("");

                if (g_verbose)
                    con::format_line("     -> {}", e->m_snapshot_path);
            }
        }

        con::write_line("");
        con::format_line("{} project(s), {} snapshot(s)", projects.size(), entries.size());
    }

    // Release entries
    for (auto* project : projects)
        PNQ_RELEASE(project);
    for (auto* entry : entries)
        PNQ_RELEASE(entry);

    return 0;
}

int cmd_list(const std::string& snapshot_path, const std::string& filter_project, bool xml_output)
{
    // If a .zip path is given, list archive contents
    if (!snapshot_path.empty())
        return cmd_list_archive(snapshot_path);

    // Otherwise list registry snapshots
    return cmd_list_registry(filter_project, xml_output);
}

int cmd_verify(const std::string& source_ref, bool list_files)
{
    auto resolved = resolve_reference(source_ref);
    if (!resolved.ok())
    {
        print_error(resolved.error);
        return 1;
    }

    insti::Blueprint* bp = nullptr;
    insti::ZipSnapshotReader reader;
    bool is_instance = (resolved.type == RefType::Instance);

    if (is_instance)
    {
        bp = insti::Instance::load_from_archive(resolved.path);
        // Open reader for file-level verification
        if (!reader.open(resolved.path))
        {
            print_error("Failed to open snapshot for verification: " + resolved.path);
            if (bp) bp->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }
    }
    else
    {
        bp = insti::Project::load_from_file(resolved.path);
    }

    if (!bp)
    {
        print_error("Failed to load blueprint: " + resolved.path);
        return 1;
    }

    con::format_line("Verifying: {} v{}", bp->project_name(), bp->project_version());
    if (is_instance)
        con::write_line("  (Instance verification - comparing file contents)");
    else
        con::write_line("  (Project verification - checking resource existence)");
    con::write_line("");

    // Setup registry
    insti::config::theSettings.load();
    std::string roots_str = insti::config::theSettings.registry.roots.get();
    insti::SnapshotRegistry registry{pnq::string::split(roots_str, ";")};
    registry.initialize();

    // Use orchestrator for verify
    // Pass reader for instance verification (file-level comparison), nullptr for project verification
    insti::Orchestrator orc{&registry};
    auto results = orc.verify(bp, nullptr, is_instance ? &reader : nullptr);

    int match_count = 0;
    int mismatch_count = 0;
    int missing_count = 0;
    int extra_count = 0;

    // Aggregate file-level counts for instance verification
    int total_file_match = 0;
    int total_file_mismatch = 0;
    int total_file_missing = 0;
    int total_file_extra = 0;

    const auto& actions = bp->actions();
    for (size_t i = 0; i < results.size() && i < actions.size(); ++i)
    {
        const auto& result = results[i];
        const auto* action = actions[i];

        const char* status_str = "";
        switch (result.status)
        {
        case insti::VerifyResult::Status::Match:
            status_str = "[MATCH]   ";
            ++match_count;
            break;
        case insti::VerifyResult::Status::Mismatch:
            status_str = "[MISMATCH]";
            ++mismatch_count;
            break;
        case insti::VerifyResult::Status::Missing:
            status_str = "[MISSING] ";
            ++missing_count;
            break;
        case insti::VerifyResult::Status::Extra:
            status_str = "[EXTRA]   ";
            ++extra_count;
            break;
        }

        con::format_line("  {} [{}] {}", status_str, action->type_name(), action->description());

        if (!result.detail.empty())
            con::format_line("             {}", result.detail);

        // Aggregate file counts
        total_file_match += result.file_match_count;
        total_file_mismatch += result.file_mismatch_count;
        total_file_missing += result.file_missing_count;
        total_file_extra += result.file_extra_count;

        // List specific files when --list is used
        if (list_files)
        {
            for (const auto& f : result.mismatched_files)
                con::format_line("               DIFFER: {}", f);
            for (const auto& f : result.missing_files)
                con::format_line("               MISSING: {}", f);
            for (const auto& f : result.extra_files)
                con::format_line("               EXTRA: {}", f);
        }
    }

    bp->release(REFCOUNT_DEBUG_ARGS);
    if (is_instance)
        reader.close();

    con::write_line("");

    // Summary
    con::format_line("Resource summary: {} match, {} mismatch, {} missing, {} extra",
                     match_count, mismatch_count, missing_count, extra_count);

    if (is_instance && (total_file_match > 0 || total_file_mismatch > 0 ||
                        total_file_missing > 0 || total_file_extra > 0))
    {
        con::format_line("File summary: {} match, {} differ, {} missing, {} extra",
                         total_file_match, total_file_mismatch, total_file_missing, total_file_extra);
    }

    con::write_line("");

    // Overall judgment
    if (mismatch_count == 0 && missing_count == 0 && extra_count == 0)
    {
        con::write_line("Status: INSTALLED");
        return 0;
    }
    else if (match_count == 0)
    {
        con::write_line("Status: NOT INSTALLED");
        return 1;
    }
    else
    {
        con::write_line("Status: PARTIALLY INSTALLED");
        return 1;
    }
}

int main(int argc, char* argv[])
{
    // Load settings and initialize logging
    insti::config::theSettings.load();
    insti::config::initialize_logging();

    argparse::ArgumentParser program("insti", insti::version());
    program.add_description("Application state snapshot and restore utility");

    // Global options
    program.add_argument("-v", "--verbose")
        .help("Enable verbose output")
        .default_value(false)
        .implicit_value(true);

    // Subcommands
    argparse::ArgumentParser backup_cmd("backup");
    backup_cmd.add_description("Create a snapshot from blueprint (shutdown -> backup -> startup)");
    backup_cmd.add_argument("blueprint")
        .help("Path to blueprint XML file, or A/B/C for project, or 1/2/3 for instance");
    backup_cmd.add_argument("output")
        .help("Output snapshot file (.zip), or omit for auto-naming")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string{});
    backup_cmd.add_argument("-f", "--force")
        .help("Run force-only hooks (aggressive termination)")
        .default_value(false)
        .implicit_value(true);
    backup_cmd.add_argument("-d", "--description")
        .help("Description for this snapshot (overrides blueprint)")
        .default_value(std::string{});

    argparse::ArgumentParser restore_cmd("restore");
    restore_cmd.add_description("Restore from a snapshot (restore -> startup)");
    restore_cmd.add_argument("snapshot")
        .help("Path to .zip, or 1/2/3 for instance");
    restore_cmd.add_argument("--dest")
        .help("Override destination path")
        .default_value(std::string{});
    restore_cmd.add_argument("--var")
        .help("Override variable: NAME=VALUE (repeatable)")
        .append()
        .default_value(std::vector<std::string>{});
    restore_cmd.add_argument("-f", "--force")
        .help("Run force-only hooks")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser list_cmd("list");
    list_cmd.add_description("List registry snapshots or archive contents");
    list_cmd.add_argument("snapshot")
        .help("Path to snapshot file (.zip), or omit to list registry")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string{});
    list_cmd.add_argument("--project")
        .help("Filter by project name")
        .default_value(std::string{});
    list_cmd.add_argument("--xml")
        .help("Dump blueprint XML from each snapshot")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser clean_cmd("clean");
    clean_cmd.add_description("Remove resources defined in blueprint or snapshot (shutdown -> clean)");
    clean_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip), or A/B/C or 1/2/3");
    clean_cmd.add_argument("-f", "--force")
        .help("Run force-only shutdown hooks (aggressive termination)")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser verify_cmd("verify");
    verify_cmd.add_description("Verify resources against live system");
    verify_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip), or A/B/C or 1/2/3");
    verify_cmd.add_argument("-l", "--list")
        .help("List individual files that differ, are missing, or extra")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser startup_cmd("startup");
    startup_cmd.add_description("Run startup hooks (start the application)");
    startup_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip), or A/B/C or 1/2/3");
    startup_cmd.add_argument("-f", "--force")
        .help("Run force-only hooks")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser shutdown_cmd("shutdown");
    shutdown_cmd.add_description("Run shutdown hooks (stop the application)");
    shutdown_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip), or A/B/C or 1/2/3");
    shutdown_cmd.add_argument("-f", "--force")
        .help("Run force-only hooks (aggressive termination)")
        .default_value(false)
        .implicit_value(true);

    program.add_subparser(backup_cmd);
    program.add_subparser(restore_cmd);
    program.add_subparser(clean_cmd);
    program.add_subparser(verify_cmd);
    program.add_subparser(startup_cmd);
    program.add_subparser(shutdown_cmd);
    program.add_subparser(list_cmd);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err)
    {
        print_error(err.what());
        con::write_line("");
        con::write_line(program.help().str());
        return 1;
    }

    g_verbose = program.get<bool>("--verbose");
    if (g_verbose)
        spdlog::set_level(spdlog::level::debug);  // Override config level when verbose

    // Print banner
    con::format_line("insti v{}", insti::version());
    con::write_line("");

    if (program.is_subcommand_used("backup"))
        return cmd_backup(backup_cmd.get<std::string>("blueprint"),
                         backup_cmd.get<std::string>("output"),
                         backup_cmd.get<bool>("--force"),
                         backup_cmd.get<std::string>("--description"));

    if (program.is_subcommand_used("restore"))
        return cmd_restore(restore_cmd.get<std::string>("snapshot"),
                          restore_cmd.get<std::string>("--dest"),
                          restore_cmd.get<std::vector<std::string>>("--var"),
                          restore_cmd.get<bool>("--force"));

    if (program.is_subcommand_used("clean"))
        return cmd_clean(clean_cmd.get<std::string>("source"),
                        clean_cmd.get<bool>("--force"));

    if (program.is_subcommand_used("verify"))
        return cmd_verify(verify_cmd.get<std::string>("source"),
                         verify_cmd.get<bool>("--list"));

    if (program.is_subcommand_used("startup"))
        return cmd_startup(startup_cmd.get<std::string>("source"),
                          startup_cmd.get<bool>("--force"));

    if (program.is_subcommand_used("shutdown"))
        return cmd_shutdown(shutdown_cmd.get<std::string>("source"),
                           shutdown_cmd.get<bool>("--force"));

    if (program.is_subcommand_used("list"))
        return cmd_list(list_cmd.get<std::string>("snapshot"),
                       list_cmd.get<std::string>("--project"),
                       list_cmd.get<bool>("--xml"));

    // No subcommand - default to list
    return cmd_list("", "", false);
}
