# Blueprint File Schema (Phase 1 POC)

## Overview

The blueprint file defines what constitutes an application's state. XML format.

## Schema

```xml
<?xml version="1.0" encoding="UTF-8"?>
<blueprint name="MyApplication" version="1.0.0">

    <description>Optional description text</description>

    <variables>
        <var name="INSTALL_DIR">${PROGRAMFILES}\MyApp</var>
        <var name="DATA_DIR">${INSTALL_DIR}\data</var>
    </variables>

    <resources>
        <files path="${INSTALL_DIR}" archive="program"/>
        <files path="${DATA_DIR}" archive="userdata"/>
    </resources>

</blueprint>
```

## Elements

### `<blueprint>` (root)

| Attribute | Required | Description |
|-----------|----------|-------------|
| `name` | Yes | Display name |
| `version` | Yes | Semantic version |

### `<description>`

Optional. Free-form text describing the blueprint.

### `<variables>`

Container for user-defined variables. Resolution order: dependency-sorted with cycle detection.

#### `<var>`

| Attribute | Required | Description |
|-----------|----------|-------------|
| `name` | Yes | Variable name (referenced as `${NAME}`) |

Element content is the value. Can reference built-ins or previously defined variables.

### `<resources>`

Container for resource definitions. Processed in document order for backup/restore, reverse order for clean.

#### `<files>` (POC)

| Attribute | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Filesystem path. Supports variable substitution. |
| `archive` | Yes | Relative path within `files/` in snapshot. |

**Backup:** Recursively copy `path` → `files/{archive}/` in archive.
**Restore:** Extract `files/{archive}/` → `path`.
**Clean:** Delete `path` recursively.

## Built-in Variables

Populated automatically from target machine:

| Variable | Source |
|----------|--------|
| `${PROGRAMFILES}` | `SHGetKnownFolderPath(FOLDERID_ProgramFiles)` |
| `${PROGRAMFILES_X86}` | `SHGetKnownFolderPath(FOLDERID_ProgramFilesX86)` |
| `${PROGRAMDATA}` | `SHGetKnownFolderPath(FOLDERID_ProgramData)` |
| `${APPDATA}` | `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` |
| `${LOCALAPPDATA}` | `SHGetKnownFolderPath(FOLDERID_LocalAppData)` |
| `${COMPUTERNAME}` | `GetComputerNameW()` |
| `${USERNAME}` | `GetUserNameW()` |
| `${WINDIR}` | `GetWindowsDirectoryW()` |
| `${SYSTEMDRIVE}` | Environment variable |

## Future Elements (Post-POC)

```xml
<blueprint name="FullExample" version="1.0.0">

    <variables>
        <var name="APP_ROOT">${PROGRAMFILES}\MyApp</var>
        <var name="SERVICE_NAME">MyAppService</var>
    </variables>

    <resources>
        <files path="${APP_ROOT}" archive="program"/>
        <registry key="HKLM\SOFTWARE\MyApp" archive="settings"/>
        <service name="${SERVICE_NAME}" archive="service"/>
        <environment name="MYAPP_HOME" scope="user"/>
    </resources>

    <hooks>
        <kill phase="PreBackup PreRestore PreClean" name="myapp.exe" timeout="5000"/>
        <process phase="PostRestore" path="${APP_ROOT}\setup.exe" wait="true">
            <arg>/silent</arg>
            <arg>/config=${APP_ROOT}\config.ini</arg>
        </process>
        <config phase="PostRestore" file="${APP_ROOT}\config.ini" format="ini">
            <set key="Database.Server">${COMPUTERNAME}</set>
            <set key="Paths.Data">${LOCALAPPDATA}\MyApp</set>
        </config>
    </hooks>

</blueprint>
```

Note: `phase` attribute supports space-separated list of phases where the hook applies.

## Example (POC Scope)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<blueprint name="Acme Widget" version="2.1.0">

    <description>Customer configuration for Acme Corp</description>

    <variables>
        <var name="ACME_ROOT">${PROGRAMFILES}\AcmeWidget</var>
        <var name="ACME_DATA">${LOCALAPPDATA}\AcmeWidget</var>
    </variables>

    <resources>
        <files path="${ACME_ROOT}" archive="program"/>
        <files path="${ACME_DATA}" archive="userdata"/>
    </resources>

</blueprint>
```
