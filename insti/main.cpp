#include "pch.h"
#include <insti/insti.h>
#include <insti/registry/snapshot_registry.h>
#include <pnq/console.h>
#include <pnq/regis3.h>
#include <argparse/argparse.hpp>
#include "settings.h"

// Color shortcuts
#define C_RESET   CONSOLE_STANDARD
#define C_BOLD    CONSOLE_FOREGROUND_BRIGHT_WHITE
#define C_DIM     CONSOLE_FOREGROUND_BRIGHT_BLACK
#define C_GREEN   CONSOLE_FOREGROUND_GREEN
#define C_YELLOW  CONSOLE_FOREGROUND_YELLOW
#define C_RED     CONSOLE_FOREGROUND_RED
#define C_CYAN    CONSOLE_FOREGROUND_CYAN

namespace con = pnq::console;

bool g_verbose = false;

void print_error(const std::string& msg)
{
    con::write(C_RED "error: " C_RESET);
    con::write_line(msg);
}

void print_success(const std::string& msg)
{
    con::write(C_GREEN);
    con::write_line(msg);
    con::write(C_RESET);
}

void print_verbose(const std::string& msg)
{
    if (g_verbose)
    {
        con::write(C_DIM);
        con::write_line(msg);
        con::write(C_RESET);
    }
}

int cmd_info(const std::string& blueprint_path)
{
    auto* bp = insti::Blueprint::load_from_file(blueprint_path);
    if (!bp)
    {
        print_error("Failed to load blueprint: " + blueprint_path);
        return 1;
    }

    con::write(C_BOLD "Blueprint: " C_RESET);
    con::write(bp->name());
    con::write(C_DIM " v" C_RESET);
    con::write_line(bp->version());

    if (!bp->description().empty())
    {
        con::write(C_DIM "Description: " C_RESET);
        con::write_line(bp->description());
    }

    con::write_line("");
    con::write_line(C_BOLD "Resolved variables:" C_RESET);
    for (const auto& [name, value] : bp->resolved_variables())
    {
        con::write("  ");
        con::write(C_CYAN);
        con::write(name);
        con::write(C_RESET " = ");
        con::write_line(value);
    }

    con::write_line("");
    con::format_line(C_BOLD "Actions ({}):" C_RESET, bp->actions().size());
    for (const auto* action : bp->actions())
    {
        con::write("  ");
        con::write(C_YELLOW "[");
        con::write(action->type_name());
        con::write("]" C_RESET " ");

        if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
        {
            con::write("path=");
            con::write(C_CYAN);
            con::write(copy_dir->path());
            con::write(C_RESET " -> archive=");
            con::write_line(copy_dir->archive_path());
            con::write(C_DIM "    resolved: ");
            con::write(bp->resolve(copy_dir->path()));
            con::write_line(C_RESET);
        }
        else if (auto* reg = dynamic_cast<const insti::RegistryAction*>(action))
        {
            con::write("key=");
            con::write(C_CYAN);
            con::write(reg->key());
            con::write(C_RESET " -> archive=");
            con::write_line(reg->archive_path());
            con::write(C_DIM "    resolved: ");
            con::write(bp->resolve(reg->key()));
            con::write_line(C_RESET);
        }
    }

    bp->release(REFCOUNT_DEBUG_ARGS);
    return 0;
}

int cmd_backup(const std::string& blueprint_path, const std::string& output_arg)
{
    auto* bp = insti::Blueprint::load_from_file(blueprint_path);
    if (!bp)
    {
        print_error("Failed to load blueprint: " + blueprint_path);
        return 1;
    }

    std::string output_path = output_arg;

    // Auto-generate output path if not specified
    if (output_path.empty())
    {
        insti::RegistrySettings settings;
        settings.load(insti::RegistrySettings::default_config_path());
        insti::SnapshotRegistry registry{pnq::string::split_stripped(settings.path.get(), ";")};

        std::string root = registry.first_writable_root();
        if (root.empty())
        {
            print_error("No writable registry root configured. Use 'insti registry add <path>' first.");
            bp->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }

        std::string filename = registry.generate_filename(
            bp->name(), std::chrono::system_clock::now());

        // Create subdirectory: root/project/version/
        std::filesystem::path dir = std::filesystem::path(root) / bp->name() / bp->version();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        output_path = (dir / filename).string();
        print_verbose("Auto-generated path: " + output_path);
    }

    con::write(C_BOLD "Backing up: " C_RESET);
    con::write(bp->name());
    con::write(C_DIM " v" C_RESET);
    con::write_line(bp->version());

    insti::ZipSnapshotWriter writer;
    if (!writer.create(output_path))
    {
        print_error("Failed to create snapshot: " + output_path);
        bp->release(REFCOUNT_DEBUG_ARGS);
        return 1;
    }

    for (const auto* action : bp->actions())
    {
        if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
        {
            std::string src_path = bp->resolve(copy_dir->path());
            std::string archive_path = "files/" + copy_dir->archive_path();

            con::write("  ");
            con::write(C_CYAN);
            con::write(src_path);
            con::write(C_DIM " -> " C_RESET);
            con::write_line(archive_path);

            if (!writer.add_directory_recursive(archive_path, src_path))
            {
                print_error("Failed to add directory: " + src_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }
        }
        else if (auto* reg = dynamic_cast<const insti::RegistryAction*>(action))
        {
            std::string key_path = bp->resolve(reg->key());
            std::string archive_path = "registry/" + reg->archive_path();

            con::write("  ");
            con::write(C_CYAN);
            con::write(key_path);
            con::write(C_DIM " -> " C_RESET);
            con::write_line(archive_path);

            // Export registry key to .reg format
            pnq::regis3::registry_importer importer{key_path};
            auto* key_entry = importer.import();
            if (!key_entry)
            {
                print_error("Failed to read registry key: " + key_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            pnq::regis3::regfile_format5_exporter exporter;
            if (!exporter.perform_export(key_entry))
            {
                print_error("Failed to export registry key: " + key_path);
                key_entry->release(REFCOUNT_DEBUG_ARGS);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            writer.write_text(archive_path, exporter.result());
            key_entry->release(REFCOUNT_DEBUG_ARGS);
        }
    }

    print_verbose("  Adding: blueprint.xml");
    writer.write_text("blueprint.xml", bp->to_xml());

    if (!writer.finalize())
    {
        print_error("Failed to finalize snapshot");
        bp->release(REFCOUNT_DEBUG_ARGS);
        return 1;
    }

    print_success("Snapshot created: " + output_path);
    bp->release(REFCOUNT_DEBUG_ARGS);
    return 0;
}

int cmd_restore(const std::string& snapshot_ref, const std::string& dest_override,
                const std::vector<std::string>& var_overrides)
{
    std::string snapshot_path = snapshot_ref;

    // If not an existing file, try to resolve via registry
    if (!std::filesystem::exists(snapshot_ref))
    {
        insti::RegistrySettings settings;
        settings.load(insti::RegistrySettings::default_config_path());
        insti::SnapshotRegistry registry{pnq::string::split_stripped(settings.path.get(), ";")};

        auto matches = registry.discover_instances(snapshot_ref);
        if (matches.empty())
        {
            print_error("Snapshot not found: " + snapshot_ref);
            return 1;
        }

        if (matches.size() > 1)
        {
            print_error("Ambiguous reference - multiple matches:");
            for (const auto* entry : matches)
            {
                con::write("  ");
                con::write_line(entry->snapshot_path());
                PNQ_RELEASE(entry);
            }
            return 1;
        }

        snapshot_path = matches[0]->snapshot_path();
        print_verbose("Resolved to: " + snapshot_path);
        PNQ_RELEASE(matches[0]);
    }

    insti::ZipSnapshotReader reader;
    if (!reader.open(snapshot_path))
    {
        print_error("Failed to open snapshot: " + snapshot_path);
        return 1;
    }

    std::string blueprint_xml = reader.read_text("blueprint.xml");
    if (blueprint_xml.empty())
    {
        print_error("No blueprint.xml in snapshot");
        return 1;
    }

    auto* bp = insti::Blueprint::load_from_string(blueprint_xml);
    if (!bp)
    {
        print_error("Failed to parse blueprint from snapshot");
        return 1;
    }

    // Apply variable overrides
    for (const auto& override_str : var_overrides)
    {
        auto pos = override_str.find('=');
        if (pos == std::string::npos)
        {
            print_error("Invalid --var format (expected NAME=VALUE): " + override_str);
            bp->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }
        std::string name = override_str.substr(0, pos);
        std::string value = override_str.substr(pos + 1);
        bp->set_override(name, value);
        print_verbose("  Override: " + name + " = " + value);
    }

    con::write(C_BOLD "Restoring: " C_RESET);
    con::write(bp->name());
    con::write(C_DIM " v" C_RESET);
    con::write_line(bp->version());

    for (const auto* action : bp->actions())
    {
        if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
        {
            std::string archive_path = "files/" + copy_dir->archive_path();
            std::string dest_path = dest_override.empty()
                ? bp->resolve(copy_dir->path())
                : dest_override;

            con::write("  ");
            con::write(archive_path);
            con::write(C_DIM " -> " C_RESET);
            con::write(C_CYAN);
            con::write_line(dest_path);
            con::write(C_RESET);

            if (!reader.extract_directory_recursive(archive_path, dest_path))
            {
                print_error("Failed to extract: " + archive_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }
        }
        else if (auto* reg = dynamic_cast<const insti::RegistryAction*>(action))
        {
            std::string archive_path = "registry/" + reg->archive_path();
            std::string key_path = bp->resolve(reg->key());

            con::write("  ");
            con::write(archive_path);
            con::write(C_DIM " -> " C_RESET);
            con::write(C_CYAN);
            con::write_line(key_path);
            con::write(C_RESET);

            // Read .reg from archive
            std::string reg_content = reader.read_text(archive_path);
            if (reg_content.empty())
            {
                print_error("Failed to read from archive: " + archive_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            // Parse and import to live registry
            auto importer = pnq::regis3::create_importer_from_string(reg_content);
            if (!importer)
            {
                print_error("Failed to parse registry file: " + archive_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            auto* key_entry = importer->import();
            if (!key_entry)
            {
                print_error("Failed to import registry data: " + archive_path);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            pnq::regis3::registry_exporter exporter;
            if (!exporter.perform_export(key_entry))
            {
                print_error("Failed to write to registry: " + key_path);
                key_entry->release(REFCOUNT_DEBUG_ARGS);
                bp->release(REFCOUNT_DEBUG_ARGS);
                return 1;
            }

            key_entry->release(REFCOUNT_DEBUG_ARGS);
        }
    }

    print_success("Restore complete");
    bp->release(REFCOUNT_DEBUG_ARGS);
    return 0;
}

int cmd_clean(const std::string& source_path)
{
    insti::Blueprint* bp = nullptr;

    // Determine if source is a snapshot or blueprint file
    if (source_path.ends_with(".zip"))
    {
        insti::ZipSnapshotReader reader;
        if (!reader.open(source_path))
        {
            print_error("Failed to open snapshot: " + source_path);
            return 1;
        }

        std::string blueprint_xml = reader.read_text("blueprint.xml");
        if (blueprint_xml.empty())
        {
            print_error("No blueprint.xml in snapshot");
            return 1;
        }

        bp = insti::Blueprint::load_from_string(blueprint_xml);
    }
    else
    {
        bp = insti::Blueprint::load_from_file(source_path);
    }

    if (!bp)
    {
        print_error("Failed to load blueprint");
        return 1;
    }

    con::write(C_BOLD "Cleaning: " C_RESET);
    con::write(bp->name());
    con::write(C_DIM " v" C_RESET);
    con::write_line(bp->version());

    // Clean resources in reverse order
    const auto& actions = bp->actions();
    for (auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        const auto* action = *it;

        if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
        {
            std::string path = bp->resolve(copy_dir->path());

            con::write("  ");
            con::write(C_RED "DELETE " C_RESET);
            con::write(C_CYAN);
            con::write_line(path);
            con::write(C_RESET);

            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            if (ec)
            {
                print_error("Failed to delete: " + path + " (" + ec.message() + ")");
                // Continue with other resources
            }
        }
        else if (auto* reg = dynamic_cast<const insti::RegistryAction*>(action))
        {
            std::string key_path = bp->resolve(reg->key());

            con::write("  ");
            con::write(C_RED "DELETE " C_RESET);
            con::write(C_CYAN);
            con::write_line(key_path);
            con::write(C_RESET);

            if (!pnq::regis3::key::delete_recursive(key_path))
            {
                print_error("Failed to delete registry key: " + key_path);
                // Continue with other resources
            }
        }
    }

    print_success("Clean complete");
    bp->release(REFCOUNT_DEBUG_ARGS);
    return 0;
}

int cmd_list_archive(const std::string& snapshot_path)
{
    insti::ZipSnapshotReader reader;
    if (!reader.open(snapshot_path))
    {
        print_error("Failed to open snapshot: " + snapshot_path);
        return 1;
    }

    con::write(C_BOLD "Snapshot: " C_RESET);
    con::write(snapshot_path);
    con::format_line(C_DIM " ({} entries)" C_RESET, reader.size());
    con::write_line("");

    for (const auto& entry : reader)
    {
        if (entry.is_directory)
        {
            con::write(C_DIM "  [DIR]  " C_RESET);
            con::write(C_CYAN);
        }
        else
        {
            con::write("  [FILE] ");
        }
        con::write_line(entry.path);
        con::write(C_RESET);
    }
    return 0;
}

int cmd_list_registry(const std::string& filter_project)
{
    insti::RegistrySettings settings;
    settings.load(insti::RegistrySettings::default_config_path());
    insti::SnapshotRegistry registry{ pnq::string::split_stripped(settings.path.get(), ";") };
    registry.initialize();

    auto entries = registry.discover_instances(filter_project);
    if (entries.empty())
    {
        if (pnq::string::is_empty(settings.path.get()))
        {
            con::write_line(C_DIM "No registry roots configured." C_RESET);
            con::write_line("Use 'insti registry add <path>' to add a snapshot directory.");
        }
        else
        {
            con::write_line(C_DIM "No snapshots found." C_RESET);
        }
        return 0;
    }

    con::format_line(C_BOLD "Snapshots ({}):" C_RESET, entries.size());
    con::write_line("");

    for (const auto* entry : entries)
    {
        con::write("  ");
        con::write(C_CYAN);
        con::write(entry->name());
        con::write(C_RESET);
        con::write(C_DIM " [");
        con::write(entry->instance().timestamp_string());
        con::write_line("]" C_RESET);

        if (g_verbose)
        {
            con::write(C_DIM "    ");
            con::write_line(entry->snapshot_path());
            con::write(C_RESET);
        }
    }

    // Release entries
    for (auto* entry : entries)
        PNQ_RELEASE(entry);

    return 0;
}

int cmd_list(const std::string& snapshot_path, const std::string& filter_project)
{
    // If a .zip path is given, list archive contents
    if (!snapshot_path.empty())
        return cmd_list_archive(snapshot_path);

    // Otherwise list registry snapshots
    return cmd_list_registry(filter_project);
}

// Registry management commands
int cmd_registry_add(const std::string& path, bool readonly)
{
    insti::RegistrySettings settings;
    settings.load(insti::RegistrySettings::default_config_path());

    auto existing_path = settings.path.get();

    // Check if already exists
    if (pnq::string::contains_nocase(existing_path, path))
    {
        print_error("Root already exists: " + path);
        return 1;
    }

	existing_path += ";" + path;
    settings.path.set(existing_path);
    if (!settings.save(insti::RegistrySettings::default_config_path()))
    {
        print_error("Failed to save registry settings");
        return 1;
    }

    con::write(C_GREEN "Added root: " C_RESET);
    con::write(path);
    if (readonly)
        con::write(C_DIM " (readonly)" C_RESET);
    con::write_line("");
    return 0;
}

int cmd_registry_remove(const std::string& path)
{
    insti::RegistrySettings settings;
    settings.load(insti::RegistrySettings::default_config_path());

    auto existing_path = settings.path.get();

    if (!pnq::string::contains_nocase(existing_path, path))
    {
        print_error("Root not found: " + path);
        return 1;
    }
    pnq::string::Writer writer;
    bool first = true;
	for (const auto path_item : pnq::string::split_stripped(existing_path, ";"))
    {
        if (!pnq::string::equals_nocase(path_item, path))
        {
            if(first)
				first = false;
			else
                writer.append(";");

            writer.append(path_item);
        }
    }
    settings.path.set(writer.as_string());
    if (!settings.save(insti::RegistrySettings::default_config_path()))
    {
        print_error("Failed to save registry settings");
        return 1;
    }

    con::write(C_GREEN "Removed root: " C_RESET);
    con::write_line(path);
    return 0;
}

int cmd_registry_roots()
{
    insti::RegistrySettings settings;
    settings.load(insti::RegistrySettings::default_config_path());

	const auto root_paths = pnq::string::split_stripped(settings.path.get(), ";");

    if (root_paths.size() == 0)
    {
        con::write_line(C_DIM "No registry roots configured." C_RESET);
        con::write_line("Use 'insti registry add <path>' to add a snapshot directory.");
        return 0;
    }

    con::format_line(C_BOLD "Registry roots ({}):" C_RESET, root_paths.size());
    con::write_line("");

    for (size_t i = 0; i < root_paths.size(); ++i)
    {
        const auto& root = root_paths[i];
        con::write("  ");
        con::write(C_GREEN "[RW] " C_RESET);
        con::write_line(root);
    }
    return 0;
}

int cmd_verify(const std::string& source_path)
{
    insti::Blueprint* bp = nullptr;

    // Determine if source is a snapshot or blueprint file
    if (source_path.ends_with(".zip"))
    {
        insti::ZipSnapshotReader reader;
        if (!reader.open(source_path))
        {
            print_error("Failed to open snapshot: " + source_path);
            return 1;
        }

        std::string blueprint_xml = reader.read_text("blueprint.xml");
        if (blueprint_xml.empty())
        {
            print_error("No blueprint.xml in snapshot");
            return 1;
        }

        bp = insti::Blueprint::load_from_string(blueprint_xml);
    }
    else
    {
        bp = insti::Blueprint::load_from_file(source_path);
    }

    if (!bp)
    {
        print_error("Failed to load blueprint");
        return 1;
    }

    con::write(C_BOLD "Verifying: " C_RESET);
    con::write(bp->name());
    con::write(C_DIM " v" C_RESET);
    con::write_line(bp->version());
    con::write_line("");

    // Create context for verify (uses clean context - no reader/writer needed)
    auto* ctx = insti::ActionContext::for_clean(bp, nullptr);

    int match_count = 0;
    int mismatch_count = 0;
    int missing_count = 0;

    for (const auto* action : bp->actions())
    {
        auto result = action->verify(ctx);

        con::write("  ");
        switch (result.status)
        {
        case insti::VerifyResult::Status::Match:
            con::write(C_GREEN "[MATCH]    " C_RESET);
            ++match_count;
            break;
        case insti::VerifyResult::Status::Mismatch:
            con::write(C_YELLOW "[MISMATCH] " C_RESET);
            ++mismatch_count;
            break;
        case insti::VerifyResult::Status::Missing:
            con::write(C_RED "[MISSING]  " C_RESET);
            ++missing_count;
            break;
        case insti::VerifyResult::Status::Extra:
            con::write(C_CYAN "[EXTRA]    " C_RESET);
            break;
        }

        con::write(C_BOLD "[");
        con::write(action->type_name());
        con::write("] " C_RESET);
        con::write_line(action->description());

        if (!result.detail.empty())
        {
            con::write(C_DIM "             ");
            con::write_line(result.detail);
            con::write(C_RESET);
        }
    }

    ctx->release(REFCOUNT_DEBUG_ARGS);
    bp->release(REFCOUNT_DEBUG_ARGS);

    con::write_line("");
    con::format_line("Summary: {} match, {} mismatch, {} missing",
                     match_count, mismatch_count, missing_count);

    if (mismatch_count == 0 && missing_count == 0)
    {
        print_success("All resources verified.");
        return 0;
    }
    return 1;
}

int cmd_test()
{
    const std::string blueprint_path = "test/small.xml";
    const std::string snapshot_path = "test-roundtrip.zip";
    const std::string restore_dest = "C:/Temp/insti-roundtrip";

    // Step 1: Backup
    con::write_line(C_BOLD "=== STEP 1: BACKUP ===" C_RESET);
    std::string original_path;
    {
        auto* bp = insti::Blueprint::load_from_file(blueprint_path);
        if (!bp)
        {
            print_error("Failed to load blueprint");
            return 1;
        }

        insti::ZipSnapshotWriter writer;
        if (!writer.create(snapshot_path))
        {
            print_error("Failed to create snapshot");
            bp->release(REFCOUNT_DEBUG_ARGS);
            return 1;
        }

        for (const auto* action : bp->actions())
        {
            if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
            {
                original_path = bp->resolve(copy_dir->path());
                std::string archive_path = "files/" + copy_dir->archive_path();
                con::write("  Backing up: ");
                con::write(C_CYAN);
                con::write_line(original_path);
                con::write(C_RESET);
                writer.add_directory_recursive(archive_path, original_path);
            }
        }
        writer.write_text("blueprint.xml", bp->to_xml());
        writer.finalize();
        bp->release(REFCOUNT_DEBUG_ARGS);
        con::write("  Created: ");
        con::write_line(snapshot_path);
    }

    con::write_line("");

    // Step 2: Restore
    con::write_line(C_BOLD "=== STEP 2: RESTORE ===" C_RESET);
    {
        insti::ZipSnapshotReader reader;
        if (!reader.open(snapshot_path))
        {
            print_error("Failed to open snapshot");
            return 1;
        }

        auto* bp = insti::Blueprint::load_from_string(reader.read_text("blueprint.xml"));
        if (!bp)
        {
            print_error("Failed to parse blueprint");
            return 1;
        }

        for (const auto* action : bp->actions())
        {
            if (auto* copy_dir = dynamic_cast<const insti::CopyDirectoryAction*>(action))
            {
                std::string archive_path = "files/" + copy_dir->archive_path();
                con::write("  Restoring to: ");
                con::write(C_CYAN);
                con::write_line(restore_dest);
                con::write(C_RESET);
                reader.extract_directory_recursive(archive_path, restore_dest);
            }
        }
        bp->release(REFCOUNT_DEBUG_ARGS);
    }

    con::write_line("");

    // Step 3: Compare
    con::write_line(C_BOLD "=== STEP 3: COMPARE ===" C_RESET);
    con::write("  Original: ");
    con::write_line(original_path);
    con::write("  Restored: ");
    con::write_line(restore_dest);

    size_t original_count = 0;
    size_t restored_count = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(original_path))
        if (entry.is_regular_file()) ++original_count;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(restore_dest))
        if (entry.is_regular_file()) ++restored_count;

    con::format_line("  Original file count: {}", original_count);
    con::format_line("  Restored file count: {}", restored_count);

    con::write_line("");
    if (original_count == restored_count)
        print_success("  PASS: File counts match!");
    else
    {
        print_error("  FAIL: File count mismatch!");
        return 1;
    }

    // Cleanup
    std::filesystem::remove_all(restore_dest);
    std::filesystem::remove(snapshot_path);
    con::write_line(C_DIM "  Cleaned up temp files." C_RESET);
    return 0;
}

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("insti", insti::version());
    program.add_description("Application state snapshot and restore utility");

    // Global options
    program.add_argument("-v", "--verbose")
        .help("Enable verbose output")
        .default_value(false)
        .implicit_value(true);

    // Subcommands
    argparse::ArgumentParser info_cmd("info");
    info_cmd.add_description("Display blueprint information");
    info_cmd.add_argument("blueprint")
        .help("Path to blueprint XML file");

    argparse::ArgumentParser backup_cmd("backup");
    backup_cmd.add_description("Create a snapshot from blueprint");
    backup_cmd.add_argument("blueprint")
        .help("Path to blueprint XML file");
    backup_cmd.add_argument("output")
        .help("Output snapshot file (.zip), or omit for auto-naming")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string{});

    argparse::ArgumentParser restore_cmd("restore");
    restore_cmd.add_description("Restore from a snapshot");
    restore_cmd.add_argument("snapshot")
        .help("Path to .zip, or reference: project, filename");
    restore_cmd.add_argument("--dest")
        .help("Override destination path")
        .default_value(std::string{});
    restore_cmd.add_argument("--var")
        .help("Override variable: NAME=VALUE (repeatable)")
        .append()
        .default_value(std::vector<std::string>{});

    argparse::ArgumentParser list_cmd("list");
    list_cmd.add_description("List registry snapshots or archive contents");
    list_cmd.add_argument("snapshot")
        .help("Path to snapshot file (.zip), or omit to list registry")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string{});
    list_cmd.add_argument("--project")
        .help("Filter by project name")
        .default_value(std::string{});

    argparse::ArgumentParser clean_cmd("clean");
    clean_cmd.add_description("Remove resources defined in blueprint or snapshot");
    clean_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip)");

    argparse::ArgumentParser verify_cmd("verify");
    verify_cmd.add_description("Verify resources against live system");
    verify_cmd.add_argument("source")
        .help("Path to blueprint XML or snapshot (.zip)");

    argparse::ArgumentParser test_cmd("test");
    test_cmd.add_description("Run roundtrip test with hardcoded paths");

    // Registry management subcommand
    argparse::ArgumentParser registry_cmd("registry");
    registry_cmd.add_description("Manage snapshot registry roots");

    argparse::ArgumentParser registry_add_cmd("add");
    registry_add_cmd.add_description("Add a snapshot root directory");
    registry_add_cmd.add_argument("path")
        .help("Path to snapshot directory");
    registry_add_cmd.add_argument("--readonly")
        .help("Mark root as read-only")
        .default_value(false)
        .implicit_value(true);

    argparse::ArgumentParser registry_remove_cmd("remove");
    registry_remove_cmd.add_description("Remove a snapshot root directory");
    registry_remove_cmd.add_argument("path")
        .help("Path to remove");

    argparse::ArgumentParser registry_roots_cmd("roots");
    registry_roots_cmd.add_description("List configured root directories");

    argparse::ArgumentParser registry_index_cmd("index");
    registry_index_cmd.add_description("Rebuild index for all roots");

    registry_cmd.add_subparser(registry_add_cmd);
    registry_cmd.add_subparser(registry_remove_cmd);
    registry_cmd.add_subparser(registry_roots_cmd);
    registry_cmd.add_subparser(registry_index_cmd);

    program.add_subparser(info_cmd);
    program.add_subparser(backup_cmd);
    program.add_subparser(restore_cmd);
    program.add_subparser(clean_cmd);
    program.add_subparser(verify_cmd);
    program.add_subparser(list_cmd);
    program.add_subparser(registry_cmd);
    program.add_subparser(test_cmd);

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

    // Print banner
    con::write(C_BOLD "insti" C_RESET);
    con::write(C_DIM " v");
    con::write(insti::version());
    con::write_line(C_RESET);
    con::write_line("");

    if (program.is_subcommand_used("info"))
        return cmd_info(info_cmd.get<std::string>("blueprint"));

    if (program.is_subcommand_used("backup"))
        return cmd_backup(backup_cmd.get<std::string>("blueprint"),
                         backup_cmd.get<std::string>("output"));

    if (program.is_subcommand_used("restore"))
        return cmd_restore(restore_cmd.get<std::string>("snapshot"),
                          restore_cmd.get<std::string>("--dest"),
                          restore_cmd.get<std::vector<std::string>>("--var"));

    if (program.is_subcommand_used("clean"))
        return cmd_clean(clean_cmd.get<std::string>("source"));

    if (program.is_subcommand_used("verify"))
        return cmd_verify(verify_cmd.get<std::string>("source"));

    if (program.is_subcommand_used("list"))
        return cmd_list(list_cmd.get<std::string>("snapshot"),
                       list_cmd.get<std::string>("--project"));

    if (program.is_subcommand_used("registry"))
    {
        if (registry_cmd.is_subcommand_used("add"))
            return cmd_registry_add(registry_add_cmd.get<std::string>("path"),
                                   registry_add_cmd.get<bool>("--readonly"));
        if (registry_cmd.is_subcommand_used("remove"))
            return cmd_registry_remove(registry_remove_cmd.get<std::string>("path"));
        if (registry_cmd.is_subcommand_used("roots"))
            return cmd_registry_roots();
        // No subcommand - show registry help
        con::write_line(registry_cmd.help().str());
        return 0;
    }

    if (program.is_subcommand_used("test"))
        return cmd_test();

    // No subcommand - show help
    con::write_line(program.help().str());
    return 0;
}
