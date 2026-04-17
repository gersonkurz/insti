# insti - Project Context

## Overview
**insti** is a Windows utility for switching between different installations and configurations of applications. It captures the complete application state—files, registry, services, environment variables, and hosts entries—into a single snapshot archive. It allows for atomic context switching, uninstallation, and verification of application states.

## Architecture
The project is divided into three main components:
1.  **Shared Engine (`shared/`)**: A static library containing the core logic for orchestration, blueprints, actions (files, registry, etc.), hooks, and snapshot I/O.
2.  **CLI (`insti/`)**: The command-line interface `insti.exe` for scripting and automation.
3.  **GUI (`instinctiv/`)**: A graphical user interface `instinctiv.exe` built with Dear ImGui (DirectX backend).

**Key Technologies:**
-   **Language:** C++23
-   **Build System:** CMake (wrapped by `build.cmd`), Visual Studio Solutions (`.slnx`, `.vcxproj`)
-   **Dependencies:** Statically linked (argparse, imgui, indicators, etc.). No external DLLs required.
-   **Platform:** Windows (x64, ARM64, Win32).

## Building and Running

### Build Command (`build.cmd`)
The project uses a helper script `build.cmd` to manage CMake builds.

*   **Build Release (Default):**
    ```cmd
    build release
    ```
    or simply `build`.

*   **Build Debug:**
    ```cmd
    build debug
    ```

*   **Run Tests:**
    ```cmd
    build test
    ```
    *Note: This executes `engramma_tests.exe` found in the build directory.*

*   **Clean Build Directory:**
    ```cmd
    build clear
    ```

### Manual CMake
You can also use CMake directly if preferred, though `build.cmd` is the recommended convenience wrapper.

## Development Conventions

*   **Code Style:** Direct, concise, and technical. Comments should explain *why*, not *what*.
*   **Linking:** All dependencies are statically linked. The output is a standalone executable.
*   **Testing:** Tests are located in the `tests/` directory (referenced by build script) and run via `build test`.
*   **Conventions:**
    -   **Blueprints:** XML files defining the application state to capture.
    -   **Snapshots:** ZIP archives containing the blueprint and captured resources.
    -   **Versioning:** Follows Semantic Versioning.

## Key Files & Directories

*   `build.cmd`: Main build control script.
*   `insti/main.cpp`: Entry point for the CLI tool.
*   `instinctiv/instinctiv.cpp`: Entry point for the GUI tool.
*   `shared/include/insti/`: Public headers for the core engine.
*   `shared/src/`: Implementation of the core engine (actions, hooks, orchestration).
*   `overview.md`: Detailed architectural documentation.
*   `CLAUDE.md`: Specific instructions for AI agents (relevant for tone/style).

## Terminology
*   **Blueprint:** In-memory or XML representation of the application state definition.
*   **Instance:** A captured state with metadata (timestamp, machine, user).
*   **Snapshot:** The physical ZIP archive of an instance.
*   **Action:** A reversible operation (Backup/Restore/Clean).
*   **Hook:** A phase-specific operation (e.g., Stop Service, Run Process).
