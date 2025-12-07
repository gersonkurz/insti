# insti - Implementation Plan

## 1. Terminology

### Core Concepts

| Term | Meaning |
|------|---------|
| **Product** | Something customized for projects, highly variable by customization and version. Example: "ProAKT" or "NG1". Organizational concept only - not modeled in the engine. |
| **Project** | A specific customization/version of a product. Example: "ProAKT 3.7 Prepackaged" or "ProAKT 3.6 for IBM". |
| **Project Metadata** | Name + short human-readable description of a project. |
| **Project Instructions** | Actions, hooks, and variable definitions that define a project. |
| **Project Blueprint** | Project metadata + project instructions. The reusable template. |
| **Instance** | The deployed state on a machine. What's actually on disk/registry/services at a specific moment. |
| **Instance Metadata** | Timestamp, machine name, user, human-readable description of this specific capture. |
| **Instance Blueprint** | Project blueprint + instance metadata. Describes a captured state. |
| **Snapshot** | The archive (zip). Contains instance blueprint + captured artifacts. Self-contained, portable. |
| **Blueprint** | In-memory representation (`core/blueprint.h`). Used for both project and instance blueprints. |

### Verbs

| Verb | Input | Output | Description |
|------|-------|--------|-------------|
| **Backup** | Project blueprint + live instance | Snapshot | Capture current state into archive |
| **Restore** | Snapshot | Live instance | Deploy archived state to machine |
| **Clean** | Project blueprint | (none) | Remove resources defined in blueprint |
| **Verify** | Snapshot or project blueprint | Report | Compare live state against expected |

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Interface Layer                       │
│  ┌─────────────┐                          ┌──────────────┐  │
│  │ insti    │                          │ instinctiv   │  │
│  │ (CLI)       │                          │ (GUI)        │  │
│  └──────┬──────┘                          └──────┬───────┘  │
└─────────┼────────────────────────────────────────┼──────────┘
          │                                        │
          ▼                                        ▼
┌─────────────────────────────────────────────────────────────┐
│                    Core Engine (Static Library)              │
│                            shared/                           │
│                                                              │
│  Orchestrator                                                │
│    - backup(blueprint, output) → snapshot                    │
│    - restore(snapshot) → instance                            │
│    - clean(blueprint|snapshot) → removes instance            │
│    - feedback via callback interface                         │
│                                                              │
│  Blueprint                                                   │
│    - loads from blueprint file via format readers            │
│    - owns variable resolution (built-ins + user-defined)     │
│    - contains resources and hooks                            │
│                                                              │
│  Resources (bidirectional: backup/restore/clean)             │
│    - files, registry, service, environment                   │
│                                                              │
│  Hooks (phase-specific)                                      │
│    - process, kill, config                                   │
│                                                              │
│  Snapshot (zip archive)                                      │
│    - read/write archive                                      │
│    - contains blueprint file + artifacts                     │
│                                                              │
│  Format Readers                                              │
│    - pluggable (TOML, JSON, ...)                            │
│    - ref-counted                                             │
└─────────────────────────────────────────────────────────────┘
```

**Build structure:** Static library (`shared/`) + two executables (`insti/` CLI, `instinctiv/` GUI). No DLLs.

**Target architectures:** x64 and ARM64.

---

## 3. Blueprint Internal Model

The engine operates on an in-memory Blueprint. Key design decisions:

- **UTF-8 everywhere** (`std::string`). Convert at Win32 API boundary.
- **Params as vector of pairs** - `std::vector<std::pair<std::string, std::string>>`. Allows duplicate keys for lists. Actions interpret types.
- **Resources vs Hooks** - Resources are bidirectional (backup/restore/clean). Hooks are phase-specific.
- **Variable resolution owned by Blueprint** - not a separate subsystem.
- **No separate CleanupSpec** - each resource knows how to clean itself. Cleanup runs in reverse resource order.

### Resources

Declared once. Engine handles backup (capture), restore (deploy), and clean (remove) automatically.

| Type | Purpose | Key Parameters |
|------|---------|----------------|
| `files` | Directory/file tree | `path`, `archive_path` |
| `registry` | Registry keys | `key`, `archive_path` |
| `service` | Windows service | `name`, `archive_path` |
| `environment` | Environment variables | `name`, `scope` (user/system) |

### Hooks

Phase-specific actions. Not reversible.

| Type | Valid Phases | Purpose | Key Parameters |
|------|--------------|---------|----------------|
| `process` | any | Run external executable | `path`, `arg` (repeatable), `wait`, `ignore_exit_code` |
| `kill` | PreBackup, PreRestore, PreClean | Kill running process | `name`, `timeout` |
| `config` | PostRestore | Patch config file with variables | `file`, `format`, `xpath`/`jsonpath`/`key` |

### Phases

| Phase | When |
|-------|------|
| PreBackup | Before capturing resources |
| PostBackup | After archive created |
| PreRestore | Before cleanup/deploy |
| PostRestore | After resources deployed |
| PreClean | Before removing resources |
| PostClean | After resources removed |

### Execution Flow

**Backup:**
1. Run PreBackup hooks (kill processes, stop services)
2. For each resource (forward order): capture to archive
3. Write blueprint file to archive
4. Run PostBackup hooks

**Restore:**
1. Open archive, read blueprint file
2. Run PreRestore hooks (kill processes, stop services)
3. For each resource (reverse order): clean
4. For each resource (forward order): deploy from archive
5. Run PostRestore hooks (start services, patch configs)

**Clean:**
1. Load blueprint (from file or archive)
2. Run PreClean hooks (kill processes, stop services)
3. For each resource (reverse order): clean
4. Run PostClean hooks

---

## 4. Variable System

Owned by Blueprint. Two-phase handling:

### At Backup Time (Substitute-on-Export)

Detect known patterns in captured data, replace with placeholders:
- Built-in variables auto-detected from current machine
- User-defined variables declared in blueprint, values resolved then detected
- Longest match first to avoid partial substitution
- Case-insensitive path matching, normalized slashes

### At Restore Time (Substitute-on-Import)

Resolve placeholders to target machine values:
- Built-ins populated from target machine
- User-defined variables resolved (may reference built-ins or other user vars)
- Dependency ordering, cycle detection

### Built-in Variables

| Variable | Source |
|----------|--------|
| `${PROGRAMFILES}` | `SHGetKnownFolderPath` |
| `${PROGRAMFILES_X86}` | `SHGetKnownFolderPath` |
| `${PROGRAMDATA}` | `SHGetKnownFolderPath` |
| `${APPDATA}` | `SHGetKnownFolderPath` |
| `${LOCALAPPDATA}` | `SHGetKnownFolderPath` |
| `${COMPUTERNAME}` | `GetComputerName` |
| `${USERNAME}` | `GetUserName` |
| `${WINDIR}` | `GetWindowsDirectory` |
| `${SYSTEMDRIVE}` | Environment |

### User-Defined Variables

Declared in blueprint, can reference built-ins or other user variables:

```toml
[variables]
INSTALL_DIR = "${PROGRAMFILES}\\MyApp"
DATA_DIR = "${INSTALL_DIR}\\data"
```

### Placeholder Syntax

`${VARNAME}` - not `%VAR%` to avoid collision with Windows environment variable expansion.

---

## 5. Blueprint File Format

### Reader Interface

Pluggable format readers. Ref-counted. Each reader handles one extension.

Responsibilities:
- `getExtension()` - returns extension this reader handles (e.g., "toml")
- `parse(content)` - deserialize to Blueprint, returns allocated object (caller owns ref)
- `serialize(blueprint)` - serialize Blueprint to string

### Format Registry

Maintains registered readers. Lookup by extension.

### Archive Structure

```
snapshot.zip
├── blueprint.toml      (or .json, .xml, etc. - exactly one)
├── files/
│   └── ...             (captured file trees)
├── registry/
│   └── ...             (exported registry data)
└── services/
    └── ...             (service definitions)
```

---

## 6. Orchestrator

Three operations: backup, restore, clean.

### Feedback Callback

Interface (not a single callback function) for:
- Progress reporting (phase, detail, percent)
- Warnings
- Errors with decision request (continue, skip, retry, abort)
- Prompts for user input

CLI implementation: may auto-abort on errors.
GUI implementation: may show dialogs, let user decide.

The callback drives abort behavior - no separate `abort()` method.

---

## 7. Development Phases

| Phase | Scope |
|-------|-------|
| **Phase 1: POC** | Files resource only, built-in variables, backup/restore/clean, minimal CLI |
| **Phase 2: Feature Complete Library** | All resources, all hooks, orchestrator, content variable substitution |
| **Phase 3: CLI Complete** | Snapshot registry, CLI polish, user experience |
| **Phase 4: GUI Complete** | Dear ImGui interface |

---

## 8. Directory Structure

```
insti/
├── CMakeLists.txt              (top-level, includes subdirs)
├── shared/
│   ├── CMakeLists.txt          (static library)
│   ├── include/
│   │   └── insti/
│   │       └── ...             (public headers)
│   └── src/
│       ├── blueprint.cpp
│       ├── snapshot.cpp
│       ├── orchestrator.cpp
│       ├── resources/
│       │   └── files.cpp
│       ├── hooks/
│       │   └── ...
│       ├── actions/
│       │   └── ...
│       └── format/
│           └── toml_reader.cpp
├── insti/
│   ├── CMakeLists.txt          (CLI executable)
│   └── main.cpp
├── instinctiv/
│   ├── CMakeLists.txt          (GUI executable)
│   └── main.cpp
├── third_party/
│   └── ...
└── tests/
    └── ...
```

GUI folder (`instinctiv/`) stays minimal until Phase 4.

---

## 9. Use Cases

### Backup of a New Installation

**Scenario:** Clean machine, project installed and verified working, user wants to capture state.

**Input:** Project blueprint (from registry)

**Flow:**
1. User selects project blueprint from registry
2. User enters instance description (optional) via backup options dialog
3. Engramma adds automatic metadata (timestamp, machine name, username)
4. Engramma executes backup using project blueprint
5. Snapshot created with auto-generated filename, added to registry

**Requirements:**
- Backup action available on any project blueprint
- Backup options dialog for instance description
- Filename auto-generated (review current `${project}-${timestamp}` pattern)
- Instance blueprint written to `${INSTALLDIR}/blueprint.xml` during backup
- CopyDirectory excludes `blueprint.xml` files to avoid capturing the instance blueprint as an artifact

### Subsequent Backup (Modified Installation)

**Scenario:** Installation exists, user modified files, wants new snapshot.

**Input:** Project blueprint (same as initial backup)

**Flow:** Same as above. The instance blueprint in `${INSTALLDIR}` identifies what's installed, but backup always uses the canonical project blueprint from registry.

**Key point:** Instance blueprints are point-in-time captures. New backup = new instance blueprint with fresh metadata.

### Clean

**Scenario:** User wants to remove an installation, regardless of current state.

**Input:** Project blueprint

**Flow:**
1. User selects project blueprint
2. Clean executes based on blueprint's resource definitions
3. Works even for partial/aborted installations

**Requirements:**
- Clean always available on project blueprints
- Dry-run option exposed in UI

### Restore

**Scenario:** User wants to deploy a previously captured state.

**Input:** Snapshot (contains instance blueprint)

**Flow:**
1. User selects snapshot from registry
2. Restore executes: pre-clean, deploy artifacts, write instance blueprint to `${INSTALLDIR}`
3. Installation registry updated

**Requirements:**
- Restore always available on snapshots
- User decides when to restore (no "already installed" blocking logic)
- Dry-run option exposed in UI

### Verify

**Scenario:** User wants to check if live state matches a snapshot or blueprint.

**Input:** Snapshot or project blueprint

**Flow:**
1. User selects snapshot or project blueprint
2. Verify compares live state against expected
3. Report shows differences only (not matches)

---

## 10. Registry Redesign

### Overview

The registry manages two types of entries:
- **Project blueprints** (`.xml` files) - reusable templates
- **Snapshots** (`.zip` files containing `blueprint.xml`) - captured instances

### Class Hierarchy

```
BlueprintBase (ABC)
├── ProjectBlueprint
└── InstanceBlueprint : ProjectBlueprint  // adds instance metadata
```

Both hold full `Blueprint*` from `core/blueprint.h`.

**Removed:**
- `SnapshotEntry` - replaced by `InstanceBlueprint`
- `SnapshotIndex` - no longer needed

**Retained:**
- `RegistrySettings` - convenience helper for TOML config persistence (clients can use it)
- `SnapshotRegistry` constructor takes `std::vector<RegistryRoot>` (plain data), not `RegistrySettings&`

### SnapshotRegistry Interface

```cpp
class SnapshotRegistry {
public:
    explicit SnapshotRegistry(std::vector<RegistryRoot> roots, NamingPattern naming = {});

    // Initialization - scans folders, builds cache
    void initialize();  // Long-running, call once at startup
    void refresh();     // Re-scan after external changes

    // Queries (fast, use cached data)
    std::vector<ProjectBlueprint*> project_blueprints() const;
    std::vector<InstanceBlueprint*> instance_blueprints() const;

    // Installation detection
    InstanceBlueprint* installed_instance() const;  // nullptr if none

    // Cache updates after operations
    void notify_backup_complete(const std::string& snapshot_path);
    void notify_restore_complete(const std::string& install_dir);
    void notify_clean_complete();

    // Existing functionality
    std::string first_writable_root() const;
    std::string generate_filename(...) const;
};
```

### Caching Strategy

- SQLite database in `%LOCALAPPDATA%\insti\cache.db`
- Stores parsed blueprint data keyed by lowercase path
- Tracks file timestamp + size for cache invalidation
- `initialize()` compares filesystem against cache, updates as needed

### Installation Detection

1. Gather all `${INSTALLDIR}` paths from instance blueprints
2. Build `std::unordered_set<std::string>` of unique paths
3. For each path, check if `blueprint.xml` exists
4. If found, compare against instance blueprints to identify exact match
5. Return matching `InstanceBlueprint*` or nullptr

---

## 11. Milestones

### Milestone 11.1: Blueprint Class Hierarchy

**Goal:** Introduce `ProjectBlueprint` and `InstanceBlueprint` classes.

**Deliverables:**
- `BlueprintBase` ABC with common interface
- `ProjectBlueprint` class holding `Blueprint*` + project metadata
- `InstanceBlueprint` derived from `ProjectBlueprint`, adds instance metadata (timestamp, machine, user, description)
- Instance metadata struct definition
- Unit tests for new classes

**Verification:** Can construct both blueprint types, instance blueprint contains project blueprint data plus metadata.

### Milestone 11.2: Blueprint Serialization

**Goal:** Instance blueprint XML includes instance metadata section.

**Deliverables:**
- XML schema update: `<instance>` section with timestamp, machine, user, description
- Parser reads instance metadata when present (absent = project blueprint)
- Serializer writes instance metadata
- Roundtrip tests

**Verification:** Parse instance blueprint XML, metadata fields populated correctly.

### Milestone 11.3: INSTALLDIR and Blueprint Exclusion

**Goal:** Blueprints designate installation directory, CopyDirectory excludes `blueprint.xml`.

**Deliverables:**
- Blueprint schema: `installdir` attribute or variable designation
- `${INSTALLDIR}` built-in variable resolved from this
- CopyDirectory action excludes files named `blueprint.xml`
- Backup writes instance blueprint to `${INSTALLDIR}/blueprint.xml`

**Verification:** Backup creates snapshot, instance blueprint appears in install dir but not in `files/` artifact.

### Milestone 11.4: Registry Refactoring

**Goal:** `SnapshotRegistry` uses new blueprint classes, SQLite caching.

**Deliverables:**
- Remove `SnapshotEntry`, `SnapshotIndex`
- `SnapshotRegistry` returns `ProjectBlueprint*` and `InstanceBlueprint*`
- SQLite cache in `%LOCALAPPDATA%\insti\cache.db`
- `initialize()` / `refresh()` methods
- Cache invalidation by timestamp + size

**Verification:** Registry discovers both blueprint types, cache persists across restarts, invalidates on file changes.

### Milestone 11.5: Installation Detection

**Goal:** Registry identifies which instance is currently installed.

**Deliverables:**
- `installed_instance()` method on `SnapshotRegistry`
- Scans `${INSTALLDIR}` paths for `blueprint.xml`
- Matches against known instance blueprints
- `notify_*` methods for cache updates after operations

**Verification:** Install a snapshot, `installed_instance()` returns correct blueprint. Clean, returns nullptr.

### Milestone 11.6: UI Integration

**Goal:** instinctiv uses redesigned registry, updated terminology.

**Deliverables:**
- Snapshot browser shows project blueprints and snapshots separately (or combined with type indicator)
- Backup available on project blueprints only
- Restore available on snapshots only
- Clean available on project blueprints
- Installed instance highlighted
- Backup options dialog for instance description
- Dry-run checkboxes for clean/restore

**Verification:** Full workflow: select project blueprint → backup → snapshot appears → restore from snapshot → installed indicator updates.

### Milestone 11.7: First-Run Experience

**Goal:** Setup dialog when registry is empty.

**Deliverables:**
- Detect empty registry (no project blueprints, no snapshots)
- Modal setup dialog explaining situation
- Option to add registry folder
- Block main UI until at least one entry exists

**Verification:** Fresh install shows setup dialog, adding a folder with blueprints allows proceeding.

### Milestone 11.8: Polish and Edge Cases

**Goal:** Production-ready UI.

**Deliverables:**
- Keyboard shortcuts (Ctrl+R refresh, Escape close dialogs, etc.)
- Proper focus handling
- Window size/position persistence
- Drag-and-drop snapshot file onto window → opens detail
- Error handling for missing files, corrupt snapshots
- Empty state messaging (no snapshots found, no roots configured)
- Icon for executable

**Verification:** App feels responsive, handles errors gracefully, remembers window state.

### Milestone 11.9: Enhanced Verify (Content Diff)

**Goal:** Verify compares actual content, not just existence. Reports only differences.

**Problem with current verify:**
- Only checks if resources exist (files present, registry keys exist, etc.)
- Doesn't compare actual content - can't detect modified files
- Reports all resources including matches, which is noise

**Enhanced verify behavior:**
- **Files:** Compare checksums (CRC32 or MD5) against snapshot, report byte-level differences
- **Registry:** Compare exported values against snapshot, report changed/added/removed values
- **Environment:** Compare current value against snapshot value
- **Services:** Compare service config against snapshot config
- **Hosts:** Compare IP/hostname against snapshot

**Output changes:**
- Only report NON-matches: MODIFIED, MISSING, EXTRA
- Suppress MATCH entries (user doesn't care about things that are correct)
- Acts like `diff` against the snapshot

**GUI integration:**
- Verify report dialog shows only differences
- Summary: "5 resources differ from snapshot" vs "All 12 resources match"
- Detail: per-resource diff with before/after values where applicable

**CLI integration:**
- `insti verify --diff` flag for enhanced mode
- Exit code reflects presence of differences

**Deliverables:**
- `VerifyResult` extended with `expected_value`, `actual_value` fields
- Each action's `verify()` method does content comparison
- Filter logic to suppress matches in output
- GUI verify report updated
- CLI flag and output format

**Verification:** Modify a file in an installed snapshot, run verify, see only that file reported as MODIFIED with expected/actual details.
