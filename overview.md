# insti - Universal Application Context Switcher

## Project Summary

Windows utility for managing, snapshotting, and restoring application state. Captures files, registry, services, environment variables, and hosts entries into portable archives. Enables atomic context switching between customer configurations and versions.

**Status:** Core engine complete. CLI complete. GUI in progress.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Interface Layer                        │
│  ┌─────────────┐                          ┌──────────────┐  │
│  │   insti     │                          │  instinctiv  │  │
│  │   (CLI)     │                          │    (GUI)     │  │
│  └──────┬──────┘                          └──────┬───────┘  │
└─────────┼────────────────────────────────────────┼──────────┘
          │                                        │
          ▼                                        ▼
┌─────────────────────────────────────────────────────────────┐
│                 Core Engine (shared/)                       │
│                                                             │
│  Orchestrator ─── backup/restore/clean/verify               │
│       │                                                     │
│       ├── Blueprint ─── variable resolution, serialization  │
│       │      ├── Actions (resources, bidirectional)         │
│       │      └── Hooks (phase-specific)                     │
│       │                                                     │
│       └── Snapshot I/O ─── zip archives                     │
│                                                             │
│  SnapshotRegistry ─── discovery, caching, install detection │
└─────────────────────────────────────────────────────────────┘
```

**Build:** Static library + two executables. No DLLs. x64, ARM64, Win32.

---

## Core Concepts

| Term | Meaning |
|------|---------|
| **Blueprint** | In-memory representation. Owns variable resolution, actions, hooks. |
| **Project** | Blueprint loaded from a project file. Reusable template. |
| **Instance** | Blueprint with instance metadata (timestamp, machine, user). Captured state. |
| **Snapshot** | Zip archive containing instance blueprint + artifacts. Self-contained. |
| **Action** | Bidirectional operation: backup (capture), restore (deploy), clean (remove), verify. |
| **Hook** | Phase-specific operation. Not reversible. |

---

## Actions (Resources)

All implemented. Each handles backup/restore/clean/verify.

| Action | Purpose | Key Parameters |
|--------|---------|----------------|
| `CopyFile` | Single file | `source`, `dest` |
| `CopyDirectory` | Directory tree | `source`, `dest`, `include`, `exclude` |
| `Registry` | Registry keys | `key`, `archive_path` |
| `Environment` | Environment variables | `name`, `scope` (User/System) |
| `Service` | Windows services | `name` |
| `Hosts` | Hosts file entries | `ip`, `hostname` |
| `DelimitedEntry` | PATH-style values | `key`, `value`, `delimiter` |
| `MultiStringEntry` | REG_MULTI_SZ | `key`, `value` |

---

## Hooks

All implemented. Execute at specific phases.

| Hook | Purpose | Key Parameters |
|------|---------|----------------|
| `RunProcess` | Execute external process | `path`, `args`, `wait`, `ignore_exit_code` |
| `KillProcess` | Terminate process | `name`, `timeout` |
| `StartService` | Start Windows service | `name`, `wait` |
| `StopService` | Stop Windows service | `name`, `wait` |
| `Substitute` | Variable substitution in files | `file`, bidirectional |
| `Sql` | SQLite queries | `database`, `query` |

**Phases:** PreBackup, PostBackup, PreRestore, PostRestore, PreClean, PostClean

---

## Variable System

Owned by Blueprint. Two-phase handling:

**At Backup (unresolve):** Detect known patterns, replace with placeholders.
**At Restore (resolve):** Replace placeholders with target machine values.

### Built-in Variables

| Variable | Source |
|----------|--------|
| `${PROGRAMFILES}` | SHGetKnownFolderPath |
| `${PROGRAMFILES_X86}` | SHGetKnownFolderPath |
| `${PROGRAMDATA}` | SHGetKnownFolderPath |
| `${APPDATA}` | SHGetKnownFolderPath |
| `${LOCALAPPDATA}` | SHGetKnownFolderPath |
| `${COMPUTERNAME}` | GetComputerName |
| `${USERNAME}` | GetUserName |
| `${WINDIR}` | GetWindowsDirectory |
| `${SYSTEMDRIVE}` | Environment |
| `${INSTALLDIR}` | Blueprint-defined |
| `${PROJECT_NAME}` | Blueprint metadata |
| `${PROJECT_VERSION}` | Blueprint metadata |

User-defined variables can reference built-ins. Dependency ordering with cycle detection.

---

## Execution Flow

**Backup:**
1. Run PreBackup hooks
2. For each action (forward): capture to archive
3. Write instance blueprint to archive
4. Run PostBackup hooks

**Restore:**
1. Open archive, read blueprint
2. Run PreRestore hooks
3. For each action (reverse): clean
4. For each action (forward): deploy
5. Run PostRestore hooks

**Clean:**
1. Load blueprint
2. Run PreClean hooks
3. For each action (reverse): clean
4. Run PostClean hooks

---

## Snapshot Archive Structure

```
snapshot.zip
├── blueprint.xml          (instance blueprint with metadata)
├── files/                  (captured file trees)
├── registry/               (exported registry data)
└── services/               (service definitions)
```

---

## CLI Commands (insti)

| Command | Purpose |
|---------|---------|
| `backup <project>` | Create snapshot from project blueprint |
| `restore <snapshot>` | Deploy snapshot to machine |
| `uninstall <project>` | Remove resources defined in blueprint |
| `verify <snapshot>` | Compare live state against snapshot |
| `startup <blueprint>` | Run startup hooks only |
| `shutdown <blueprint>` | Run shutdown hooks only |
| `list` | Show registry contents |
| `list <snapshot>` | Show archive contents |

**Reference syntax:** Letters (A/B/C) for projects, numbers (1/2/3) for instances.

---

## Directory Structure

```
insti/
├── shared/                 (static library - core engine)
│   ├── include/insti/      (public headers)
│   └── src/
│       ├── core/           (blueprint, orchestrator, context)
│       ├── actions/        (9 action implementations)
│       ├── hooks/          (6 hook implementations)
│       ├── snapshot/       (zip reader/writer)
│       └── config/         (settings, registry)
├── insti/                  (CLI executable)
├── instinctiv/             (GUI executable)
├── third_party/            (dependencies)
├── setup/                  (NSIS installers)
└── tests/
```

---

## Key Source Files

| Purpose | Location |
|---------|----------|
| Blueprint class | `shared/include/insti/blueprint.h`, `shared/src/core/blueprint.cpp` |
| Orchestrator | `shared/include/insti/orchestrator.h`, `shared/src/core/orchestrator.cpp` |
| Actions | `shared/src/actions/*.cpp` |
| Hooks | `shared/src/hooks/*.cpp` |
| Snapshot I/O | `shared/src/snapshot/*.cpp` |
| CLI | `insti/main.cpp` |
| GUI | `instinctiv/instinctiv.cpp`, `instinctiv/app_state.cpp` |

---

## Implementation Status

### Complete

- All 8 action types (files, registry, service, environment, hosts, delimited, multistring)
- All 6 hook types (process, kill, start/stop service, substitute, sql)
- Variable system (resolution, unresolve, overrides, built-ins, user-defined)
- Backup/restore/clean/verify orchestration
- Snapshot I/O (zip archives)
- CLI with all commands
- Instance metadata (timestamp, machine, user, description)
- Registry discovery and caching

### In Progress

- GUI (instinctiv) - rendering infrastructure complete, dialogs being built
- Enhanced content-level verification (file checksums)
- Blueprint editor UI

---

## Blueprint XML Schema

```xml
<?xml version="1.0" encoding="UTF-8"?>
<blueprint version="1.0">
    <metadata>
        <name>Project Name</name>
        <version>1.0.0</version>
        <description>Human-readable description</description>
        <installdir>${PROGRAMFILES}\MyApp</installdir>
    </metadata>

    <!-- Instance metadata (present in snapshots only) -->
    <instance>
        <timestamp>2024-01-15T10:30:00Z</timestamp>
        <machine>WORKSTATION</machine>
        <user>admin</user>
        <description>Initial capture</description>
    </instance>

    <variables>
        <variable name="CUSTOM_VAR" value="${INSTALLDIR}\data"/>
    </variables>

    <actions>
        <copy-directory source="${INSTALLDIR}" dest="files/app"/>
        <registry key="HKLM\SOFTWARE\MyApp" archive="registry/app.reg"/>
        <service name="MyAppService"/>
        <environment name="MYAPP_HOME" scope="System"/>
    </actions>

    <hooks>
        <kill-process name="myapp.exe" phase="PreBackup"/>
        <run-process path="${INSTALLDIR}\setup.exe" phase="PostRestore" wait="true"/>
        <substitute file="${INSTALLDIR}\config.ini" phase="PostRestore"/>
    </hooks>
</blueprint>
```
