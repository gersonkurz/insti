# Snapshot Registry - Design Document

## Problem Statement

Developers and testers need to manage multiple snapshots across projects, versions, and variants. They need to:
- Quickly find and restore specific snapshots
- Maintain local working snapshots alongside shared team snapshots
- Use snapshots as "undo points" during testing iterations

## Design Principles

1. **No database** - Discovery via directory enumeration. No sync issues, no setup.
2. **Self-describing filenames** - Snapshot identity encoded in filename. No need to open zip.
3. **Folder hierarchy as structure** - Natural organization via directories.
4. **Multiple roots** - Local folders + network shares + cloud sync folders.

## Architecture

### Registry Configuration

Location: `%APPDATA%\insti\registry.toml`

Uses `pnq::config::Section` for schema-as-code with TOML serialization:

```cpp
struct RootEntry : public pnq::config::Section
{
    RootEntry(Section* parent, std::string name) : Section{parent, std::move(name)} {}

    TypedValue<std::string> path{this, "Path", ""};
    TypedValue<bool> writable{this, "Writable", true};
};

struct RegistrySettings : public pnq::config::Section
{
    RegistrySettings() : Section{} {}

    struct NamingSettings : public Section
    {
        NamingSettings(Section* parent) : Section{parent, "Naming"} {}

        TypedValue<std::string> pattern{this, "Pattern", "${project}-${variant}-${version}-${timestamp}"};
        // timestamp format is fixed: YYYYMMDD-HHMMSS (no config needed)
    } naming{this};

    TypedValueVector<RootEntry> roots{this, "Roots"};
};
```

Resulting TOML:

```toml
[Naming]
Pattern = "${project}-${variant}-${version}-${timestamp}"

[Roots/0]
Path = "D:\\Snapshots"
Writable = true

[Roots/1]
Path = "\\\\server\\share\\Snapshots"
Writable = false

[Roots/2]
Path = "C:\\Users\\dev\\Google Drive\\Snapshots"
Writable = true
```

**Note:** `${name}` syntax aligns with blueprint variables. Timestamp format is fixed (`YYYYMMDD-HHMMSS`) to avoid confusion with `strftime` syntax.

### Filename Format

Default pattern: `${project}-${variant}-${version}-${timestamp}.zip`

Example: `ProAKT-CustomerA-v2.3-20251201-143022.zip`

**Tokens:**
- `${project}` - From blueprint `name` attribute
- `${variant}` - User-specified or "default"
- `${version}` - From blueprint `version` attribute
- `${timestamp}` - Backup creation time (fixed format: `YYYYMMDD-HHMMSS`)
- `${machine}` - Optional, for developer-specific snapshots

**Parsing:** Regex-based, pattern determines capture groups.

### Directory Structure

```
D:\Snapshots\
├── ProAKT\
│   ├── v2.2\
│   │   ├── ProAKT-CustomerA-v2.2-20251101-091500.zip
│   │   └── ProAKT-CustomerB-v2.2-20251102-140000.zip
│   └── v2.3\
│       ├── ProAKT-CustomerA-v2.3-20251201-143022.zip
│       └── ProAKT-default-v2.3-20251201-150000.zip
└── OtherProject\
    └── ...
```

Subdirectories are optional - discovery scans recursively. Structure is purely organizational.

### Snapshot Identification

Full path makes each snapshot unique. Two snapshots with identical names in different roots are listed separately:

```
insti list
  D:\Snapshots\ProAKT\v2.3\ProAKT-CustomerA-v2.3-20251201-143022.zip
  \\server\share\Snapshots\ProAKT\v2.3\ProAKT-CustomerA-v2.3-20251201-143022.zip
```

### CLI Integration

**List snapshots (overloaded):**
```
insti list                                    # list all registered snapshots
insti list --project ProAKT                   # filter by project
insti list --project ProAKT --variant CustomerA
insti list <archive.zip>                      # list archive contents (existing behavior)
```

**Restore by reference:**
```
insti restore ProAKT/CustomerA/latest
insti restore ProAKT/CustomerA/v2.3
insti restore "ProAKT-CustomerA-v2.3-20251201-143022"
```

**Backup with auto-naming:**
```
insti backup blueprint.xml --variant CustomerA
# Creates: {first_writable_root}/{project}/{version}/{pattern}.zip
```

**Manage roots:**
```
insti registry add "D:\MySnapshots" [--readonly]
insti registry remove "D:\MySnapshots"
insti registry roots
```

### Resolution Rules

When restoring by reference:
1. Parse reference: `project/variant/version` or `project/variant/latest` or filename
2. Search all roots for matches
3. If multiple matches: list all with paths, prompt user or require `--path` flag
4. `latest` resolves to most recent timestamp within matching criteria

### Index Cache

Each root gets an `.insti-index.toml` with extracted metadata:

```toml
# Auto-generated index - delete to rebuild
generated = 2025-12-01T14:30:22

[[snapshots]]
filename = "ProAKT-CustomerA-v2.3-20251201-143022.zip"
project = "ProAKT"
version = "2.3"
variant = "CustomerA"
description = "Customer A production config"
timestamp = 2025-12-01T14:30:22
size = 1234567
mtime = 2025-12-01T14:30:22  # for freshness check
checksum = "sha256:a1b2c3d4..."  # integrity verification

[[snapshots]]
filename = "ProAKT-CustomerB-v2.3-20251201-150000.zip"
# ...
```

**Benefits:**
- No per-snapshot sidecar files - single index per root
- Self-healing: new/modified zips detected by mtime, re-scanned automatically
- Rich metadata (description, resources) without opening every zip

**Freshness check:**
- Compare each entry's `mtime` against actual file mtime
- New files (not in index) → scan and add
- Modified files (mtime changed) → re-scan
- Deleted files → remove from index

**Behavior:**

- **CLI `insti list`**: Use index if available. Missing/stale entries scanned on demand.
- **CLI `insti registry index`**: Full rebuild of all root indexes.
- **GUI**: Background thread builds/refreshes index on startup. Enables instant filtering/search.

---

## Implementation Milestones

### M15.1: Registry Configuration

- `RegistrySettings` struct using `pnq::config::Section`
- `RootEntry` section with path and writable fields
- `NamingSettings` section with pattern
- Load/save via `pnq::config::TomlBackend`
- Default config creation on first run

**Files:**
- `shared/include/insti/registry/settings.h`
- `shared/src/registry/settings.cpp`
- `tests/test_registry_settings.cpp`

### M15.2: Snapshot Discovery

- `SnapshotEntry` struct (path, project, variant, version, timestamp, description, size, mtime, checksum)
- `SnapshotRegistry::discover()` - enumerate all roots recursively
- Filename parsing based on configured pattern
- Filter methods: by project, variant, version

**Files:**
- `shared/include/insti/registry/entry.h`
- `shared/include/insti/registry/registry.h`
- `shared/src/registry/registry.cpp`
- `tests/test_snapshot_registry.cpp`

### M15.3: Index Cache

- `.insti-index.toml` per root with full metadata
- `SnapshotIndex` class: load, save, update
- Freshness check via mtime comparison
- Incremental update: scan only new/modified files
- Extract metadata from blueprint.xml inside zip
- SHA256 checksum for integrity verification

**Files:**
- `shared/include/insti/registry/index.h`
- `shared/src/registry/index.cpp`
- `tests/test_snapshot_index.cpp`

### M15.4: Reference Resolution

- `SnapshotRegistry::resolve(reference)` - returns matching entries
- Support for `project/variant/version`, `project/variant/latest`, exact filename
- Ambiguity detection (multiple matches)

**Files:**
- Extend `registry.h/cpp`
- Additional test cases

### M15.5: CLI Commands

- Overload `insti list` for registry (no args = registry, with .zip = archive contents)
- `insti registry add/remove/roots` for root management
- `insti registry index` - rebuild all indexes
- Modify `insti backup` to support `--variant` and auto-naming
- Modify `insti restore` to accept references

**Files:**
- `insti/main.cpp` - new subcommands

### M15.6: Documentation

- User documentation for registry usage
- Examples for common workflows

---

## Open Questions (Resolved)

**Q: Database or no database?**
A: No database. Directory enumeration is sufficient, avoids sync complexity.

**Q: Filename format fixed or configurable?**
A: Configurable pattern with tokens. Default provides reasonable structure.

**Q: Caching?**
A: CLI always fresh. GUI caches in memory with manual refresh.

**Q: Conflict handling for same filename in multiple roots?**
A: List all with full paths. User selects. Full path is unique identifier.

---

## Future Considerations (Out of Scope)

- Cloud storage integration (S3, Azure Blob) - would require new SnapshotReader/Writer
- Snapshot cleanup policies (retention rules)
