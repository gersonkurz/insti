#pragma once

// =============================================================================
// insti.h - Master include for the insti library
// =============================================================================
//
// OVERVIEW
// --------
// insti is a Windows utility for managing, snapshotting, and restoring
// application state (files, registry, services, environment variables).
//
// TERMINOLOGY
// -----------
// - Blueprint:  In-memory representation of a snapshot recipe. Owns variable
//               resolution and defines what resources/hooks to process.
//
// - Snapshot:   Zip archive containing a serialized blueprint plus artifacts
//               (backed-up files, registry exports, etc.).
//
// - Action:     A resource operation (backup/restore/clean). Implementations:
//               CopyFileAction, CopyDirectoryAction, RegistryAction,
//               EnvironmentAction, ServiceAction, HostsAction, etc.
//
// - Hook:       Phase-specific callback. Runs at defined points in the
//               backup/restore/clean lifecycle (PreBackup, PostRestore, etc.).
//
// - Phase:      Execution point: PreBackup, PostBackup, PreRestore,
//               PostRestore, PreClean, PostClean.
//
// - Registry:   Discovery system for locating snapshots across configured
//               root directories. Not to be confused with Windows Registry.
//
// HEADER STRUCTURE
// ----------------
// insti/
//   insti.h           <- You are here (master include)
//   core/
//     phase.h            - Phase enum and helpers
//     blueprint.h        - Blueprint class
//     orchestrator.h     - Backup/restore/clean orchestration
//     action_context.h   - Runtime context for actions
//     action_callback.h  - Progress callback interface
//   actions/
//     action.h           - IAction abstract base class
//     copy_file.h        - Single file backup/restore
//     copy_directory.h   - Directory tree backup/restore
//     registry.h         - Windows Registry operations
//     environment.h      - Environment variable operations
//     service.h          - Windows Service state
//     hosts.h            - Hosts file entries
//     delimited_entry.h  - Delimited file entries
//     multistring_entry.h - REG_MULTI_SZ entries
//   hooks/
//     hook.h             - IHook abstract base class
//     kill_process.h     - Terminate processes
//     run_process.h      - Execute processes
//     substitute.h       - Variable substitution in files
//     sql.h              - SQLite query execution
//   snapshot/
//     entry.h            - Archive entry metadata
//     reader.h           - SnapshotReader ABC
//     writer.h           - SnapshotWriter ABC
//     zip_reader.h       - Zip implementation of reader
//     zip_writer.h       - Zip implementation of writer
//   registry/
//     registry.h         - SnapshotRegistry discovery
//     settings.h         - Registry configuration
//     entry.h            - SnapshotEntry metadata
//
// USAGE
// -----
// Include this header for full library access, or include individual
// headers for finer-grained control.
//

namespace insti
{
    /// Library version string.
    constexpr const char* version() { return "0.1.0"; }
}

// Core
#include <insti/core/phase.h>
#include <insti/core/blueprint.h>
#include <insti/core/project_blueprint.h>
#include <insti/core/instance_blueprint.h>
#include <insti/core/action_callback.h>
#include <insti/core/action_context.h>
#include <insti/core/orchestrator.h>

// Actions
#include <insti/actions/action.h>
#include <insti/actions/copy_directory.h>
#include <insti/actions/copy_file.h>
#include <insti/actions/registry.h>
#include <insti/actions/environment.h>
#include <insti/actions/delimited_entry.h>
#include <insti/actions/multistring_entry.h>
#include <insti/actions/service.h>
#include <insti/actions/hosts.h>

// Hooks
#include <insti/hooks/hook.h>
#include <insti/hooks/kill_process.h>
#include <insti/hooks/run_process.h>
#include <insti/hooks/substitute.h>
#include <insti/hooks/sql.h>

// Snapshot
#include <insti/snapshot/entry.h>
#include <insti/snapshot/reader.h>
#include <insti/snapshot/writer.h>
#include <insti/snapshot/zip_reader.h>
#include <insti/snapshot/zip_writer.h>

// Registry (Snapshot discovery)
#include <insti/registry/snapshot_registry.h>
