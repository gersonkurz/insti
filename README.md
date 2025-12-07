<p align="center">
  <img src="logo.png" alt="insti logo" width="1024"/>
</p>

**insti** is a Windows utility for switching between different installations and configurations of [ProAKT](https://www.pnq.de/proakt.html) (and other complex applications). It captures the complete application state — files, registry, services, environment variables — into a single snapshot archive, then restores it exactly as captured on any compatible Windows machine.

## Background

ProAKT deployments often coexist in multiple variants: different customer configurations, software versions, or test environments. Switching between them manually is error-prone and time-consuming. **insti** automates this: back up the current state, clean the system, restore a different snapshot — all in one operation.

While designed primarily for ProAKT, the architecture is generic. Any Windows application that stores state in predictable locations (files, registry, services, environment) can be managed through insti blueprints.

**Predecessor:** [insti](https://github.com/gersonkurz/insti) — a similar tool with narrower scope. insti extends the concept with variable substitution, hooks, SQL patching, and a snapshot registry.

**The name:** An *engram* (from Greek ἔγγραμμα, "inscription") is the hypothetical means by which memories are stored in the brain — a physical trace of experience. insti captures and restores the complete "memory" of an application's state.

## How It Works

### Core Concepts

| Concept | Description |
|---------|-------------|
| **Blueprint** | XML definition of what to capture: directories, registry keys, services, environment variables, and hooks to run. Lives inside the snapshot. |
| **Snapshot** | Self-contained ZIP archive with the blueprint plus all captured artifacts. Portable between machines. |
| **Variables** | Placeholders like `${COMPUTERNAME}` or `${PROGRAMFILES}` that adapt paths automatically during restore. |
| **Hooks** | Pre/post actions: kill processes, run executables, patch SQLite databases. |

### Operations

**Backup** captures the current state:
1. Run pre-backup hooks (e.g., stop the application)
2. Copy files, export registry, record services and environment
3. Bundle everything into a ZIP with the blueprint

**Restore** switches to a different state:
1. Run pre-restore hooks
2. Clean existing installation (delete files, registry keys, services)
3. Extract and apply the snapshot with variable substitution
4. Run post-restore hooks (e.g., start the application)

**Clean** removes the application entirely based on the blueprint, without restoring anything.

**Verify** compares the current system state against a blueprint, reporting what matches and what differs.

---

## Blueprint Format

A blueprint is an XML file that defines what resources to capture/restore. It lives inside the snapshot archive.

### Basic Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<blueprint name="MyApp" version="1.0">
    <description>Optional description of this blueprint</description>

    <variables>
        <!-- User-defined variables -->
    </variables>

    <resources>
        <!-- Resources to backup/restore -->
    </resources>

    <hooks>
        <!-- Actions to run at specific phases -->
    </hooks>
</blueprint>
```

### Variables

Variables allow path and value substitution. Built-in variables are automatically available:

| Variable | Description |
|----------|-------------|
| `${PROGRAMFILES}` | Program Files directory |
| `${PROGRAMFILES_X86}` | Program Files (x86) directory |
| `${PROGRAMDATA}` | ProgramData directory |
| `${APPDATA}` | Roaming AppData directory |
| `${LOCALAPPDATA}` | Local AppData directory |
| `${WINDIR}` | Windows directory |
| `${COMPUTERNAME}` | Computer name |
| `${USERNAME}` | Current user name |
| `${SYSTEMDRIVE}` | System drive (e.g., "C:") |
| `${PROJECT_NAME}` | Blueprint name attribute |
| `${PROJECT_VERSION}` | Blueprint version attribute |

User-defined variables can reference other variables:

```xml
<variables>
    <var name="INSTALL_DIR">${PROGRAMFILES}\MyCompany\MyApp</var>
    <var name="CONFIG_DIR">${APPDATA}\MyApp</var>
</variables>
```

### Resources

#### Files

Captures a directory tree:

```xml
<files path="${INSTALL_DIR}" archive="files/install"/>
<files path="${CONFIG_DIR}" archive="files/config"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Source directory path (variables supported) |
| `archive` | Yes | Path prefix in snapshot archive |

#### Registry

Captures a registry key and all subkeys/values:

```xml
<registry key="HKCU\Software\MyApp" archive="registry/user.reg"/>
<registry key="HKLM\SOFTWARE\MyCompany\MyApp" archive="registry/machine.reg"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `key` | Yes | Registry key path (HKCU, HKLM, etc.) |
| `archive` | Yes | Path in snapshot for .REG file |

#### Environment Variables

Captures a single environment variable:

```xml
<environment name="MY_APP_HOME" archive="env/home" scope="user"/>
<environment name="MY_APP_PATH" archive="env/path" scope="system"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `name` | Yes | Environment variable name |
| `archive` | Yes | Path in snapshot for value |
| `scope` | No | `user` (default) or `system` |

#### PATH Entries

Manages a single directory entry in the system PATH variable. Unlike `environment` which captures the entire value, this adds/removes a specific directory:

```xml
<pathentry directory="${INSTALL_DIR}" archive="path/install" scope="system"/>
<pathentry directory="${USERPROFILE}\bin" archive="path/userbin" scope="user"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `directory` | Yes | Directory to add/remove from PATH |
| `archive` | Yes | Path in snapshot (stores "present" or "absent") |
| `scope` | No | `system` (default) or `user` |

On backup, records whether the directory was present. On restore, adds or removes it accordingly. On clean, removes the directory from PATH.

#### Services

Captures Windows service configuration:

```xml
<service name="MyAppService" archive="services/myapp.toml"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `name` | Yes | Windows service name |
| `archive` | Yes | Path in snapshot for TOML config |

The service config captures: display name, description, binary path, start type, service type, account, dependencies, and running state.

#### Hosts Entries

Captures a specific hostname from the Windows hosts file:

```xml
<hosts hostname="myapp.local" archive="hosts/myapp.toml"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `hostname` | Yes | Hostname to capture |
| `archive` | Yes | Path in snapshot for TOML entry |

### Hooks

Hooks execute at specific phases of backup/restore/clean operations:

| Phase | Description |
|-------|-------------|
| `pre-backup` | Before backup starts |
| `post-backup` | After backup completes |
| `pre-restore` | Before restore starts |
| `post-restore` | After restore completes |
| `pre-clean` | Before clean starts |
| `post-clean` | After clean completes |

#### Kill Process

Terminates a process by name:

```xml
<kill phase="pre-backup" process="myapp.exe" timeout="5000"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `phase` | Yes | When to execute |
| `process` | Yes | Process name to kill |
| `timeout` | No | Wait timeout in ms (default: 5000) |

#### Run Process

Executes a program:

```xml
<run phase="post-restore" path="${INSTALL_DIR}\setup.exe" wait="true">
    <arg>/silent</arg>
    <arg>/norestart</arg>
</run>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `phase` | Yes | When to execute |
| `path` | Yes | Executable path |
| `wait` | No | Wait for completion (default: true) |
| `ignore-exit-code` | No | Don't fail on non-zero exit (default: false) |

#### SQL

Executes a SQL statement against a SQLite database:

```xml
<sql phase="post-restore" file="${INSTALL_DIR}\app.db" query="UPDATE config SET value='${COMPUTERNAME}' WHERE key='machine'"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `phase` | Yes | When to execute |
| `file` | Yes | Path to SQLite database (variables supported) |
| `query` | Yes | SQL statement to execute (variables supported) |

#### Substitute

Performs text substitution in a file (bidirectional):

```xml
<substitute phase="post-restore" file="${CONFIG_DIR}\settings.ini" from="OLDMACHINE" to="${COMPUTERNAME}"/>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `phase` | Yes | When to execute |
| `file` | Yes | File to modify (variables supported) |
| `from` | Yes | Text to find |
| `to` | Yes | Replacement text (variables supported) |

On backup phases, substitutes `to` → `from` (de-personalization). On restore phases, substitutes `from` → `to` (personalization).

### Complete Example

```xml
<?xml version="1.0" encoding="UTF-8"?>
<blueprint name="ProAKT" version="3.2.1">
    <description>ProAKT Application</description>

    <variables>
        <var name="PROAKT_DIR">${PROGRAMFILES}\Proakt</var>
        <var name="PROAKT_DATA">${PROGRAMDATA}\Proakt</var>
    </variables>

    <resources>
        <files path="${PROAKT_DIR}" archive="files/program"/>
        <files path="${PROAKT_DATA}" archive="files/data"/>
        <registry key="HKLM\SOFTWARE\WOW6432Node\XFS" archive="registry/xfs.reg"/>
        <registry key="HKCU\Software\Proakt" archive="registry/user.reg"/>
        <environment name="PROAKT_HOME" archive="env/home" scope="system"/>
        <pathentry directory="${PROAKT_DIR}" archive="path/proakt" scope="system"/>
        <service name="ProaktService" archive="services/proakt.toml"/>
    </resources>

    <hooks>
        <kill phase="pre-backup" process="proakt.exe"/>
        <kill phase="pre-restore" process="proakt.exe"/>
        <sql phase="post-restore" file="${PROAKT_DATA}\config.db" query="UPDATE settings SET machine='${COMPUTERNAME}'"/>
        <run phase="post-restore" path="${PROAKT_DIR}\postinstall.bat" wait="true"/>
    </hooks>
</blueprint>
```

## CLI Usage

### Basic Commands

```
insti info <blueprint.xml>                   # Show blueprint details
insti backup <blueprint.xml> [output.zip]    # Create snapshot
insti restore <snapshot>                     # Restore from snapshot
insti clean <blueprint.xml|snapshot.zip>     # Remove all resources
insti verify <blueprint.xml|snapshot.zip>    # Compare system vs blueprint
insti list [snapshot.zip]                    # List registry or archive contents
```

### Snapshot Registry

The snapshot registry tracks snapshots across multiple root directories, enabling reference-based restore instead of full paths.

#### Configure Roots

```
insti registry add <path>              # Add a writable root
insti registry add <path> --readonly   # Add a read-only root
insti registry remove <path>           # Remove a root
insti registry roots                   # List configured roots
insti registry index                   # Rebuild indexes for all roots
```

Settings stored in `%APPDATA%\insti\registry.toml`.

#### List Snapshots

```
insti list                             # List all snapshots in registry
insti list --project ProAKT            # Filter by project
insti list snapshot.zip                # List archive contents
```

#### Backup with Auto-Naming

When output path is omitted, snapshots are auto-named and placed in the first writable root:

```
insti backup blueprint.xml
```

Creates: `{root}/{project}/{version}/{project}-{timestamp}.zip`

#### Restore by Reference

Instead of full paths, use project references:

```
insti restore ProAKT/CustomerA/v2.3      # Exact version
insti restore ProAKT/CustomerA/latest    # Most recent timestamp
insti restore ProAKT-CustomerA-v2.3-20251201-143022.zip  # By filename
insti restore C:\Snapshots\snapshot.zip  # Full path still works
```

#### Variable Overrides

Override blueprint variables during restore:

```
insti restore snapshot.zip --var INSTALL_DIR=D:\Custom\Path
insti restore snapshot.zip --var INSTALL_DIR=D:\Path --var DATA_DIR=E:\Data
```

## Building

Requires: Visual Studio 2022, CMake 3.20+

```
cmake -B build-x64 -A x64
cmake --build build-x64 --config Release
```

## Testing

```
./build-x64/tests/Release/engramma_tests.exe
```
