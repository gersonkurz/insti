# Project Specification: insti - the Universal Application Context Switcher

## 1. Project Overview
We are building a Windows-only utility to manage, snapshot, and restore the state of arbitrary Windows applications. The primary function is **State Management**: allowing a user to capture a specific configuration/version of an application and restore it later, exactly as it was.

To guarantee a clean restoration, the tool implicitly handles the cleanup (uninstall/removal) of the currently active state before applying the new snapshot. This enables atomic context switching between different customer configurations, versions, or application states.

## 2. Technical Stack & Constraints
* **Language:** Modern C++ (C++20).
* **Runtime:** Statically linked executable. Zero external dependencies.
* **Target Architecture Support:** x86 (32-bit) and x64 (64-bit). The host tool is x64 but must correctly handle WOW64 redirection for file paths (`Program Files (x86)`) and Registry keys (`KEY_WOW64_32KEY`).
* **UI:** Dear ImGui (Win32/DirectX backend).
* **Configuration:** TOML.
* **Archive:** 7z.
* **Integration:** The project will utilize existing proprietary C++ libraries for Registry operations (export/import) and Unicode handling.
* **OS:** Windows 10/11 (x64).

## 3. Operational Logic

### 3.1. The "Blueprint"
A `blueprint.toml` (or similar) defines the application abstractly. It lives inside the snapshot archive, making the snapshot self-contained. It describes *what* to back up and *how* to restore.

### 3.2. Variable Substitution
The tool must support environment-agnostic restoration via "Substitute-on-Write."
* **Concept:** The Blueprint contains placeholders.
* **Execution:** During restoration, the tool resolves these placeholders to local values (paths, computer names) and injects them into configuration files and Registry entries as they are written to the target.

### 3.3. Extensible Action System
The engine relies on an abstract "Action" pattern. The Blueprint is a sequence of these Actions.
* **Requirements:** Actions must support execution, rollback, and verification.
* **Example Action Types:**
    * **FileSystem:** Copy/Delete.
    * **Registry:** Import/Export/Delete keys.
    * **Service:** SCM management (Create/Start/Stop/Delete).
    * **Environment:** PATH/EnvVar modification.
    * **External Process:** Run arbitrary tools (e.g., `shutdown_tool.exe`, `db_migrator.exe`) with arguments.
    * **Config Manipulation:** Intelligent read/write for XML, JSON, or TOML files (beyond simple regex replacement).

## 4. Lifecycle Workflows

### A. Backup (Snapshot)
1.  **Preparation:** Run configured "Pre-Backup" tools (e.g., app shutdown).
2.  **Capture:** Serialize Registry, collect files, gather service definitions.
3.  **Archive:** Compress artifacts + Blueprint into 7z.

### B. Restore
1.  **Preparation:** Run configured "Pre-Restore" tools (shutdown active instance).
2.  **Clean:** Remove the current installation (Files, Registry, Services) based on the *current* state or the *target* Blueprint to ensure a collision-free environment.
3.  **Deploy:** Unpack snapshot.
4.  **Apply:** Execute Actions (Place files, Import Registry with substitution, Register Services).
5.  **Finalize:** Run configured "Post-Restore" tools (e.g., app startup).

## 5. Instructions for the AI

**Step 0: The Planning Phase (Strict Prerequisite)**
Do not write a single line of code yet. Your first task is to generate a comprehensive `implementation-plan.md` file for human review.
* This plan acts as the source of truth.
* Coding begins only **after** the human explicitly accepts this plan.
* Be prepared to revise this plan as we encounter reality during implementation.

**Requirements for the Plan:**
1.  **Strategy:** MVP First. Prioritize the **Core Engine** and **CLI**. The GUI is a secondary phase.
2.  **Architecture:** Strict separation of concerns is mandatory. The Core Engine (Library) must be completely decoupled from the Interface (CLI/GUI). No logic spillover into the CLI.
3.  **Milestones:** Break the project into distinct, logically grouped milestones.
4.  **Granularity:** Each milestone must consist of specific, actionable tasks.
5.  **Verifiability:** Each milestone must result in a buildable, runnable, and human-verifiable artifact.
6.  **Definition:** You (the AI) must define the C++ class architecture and data structures (Blueprint schema) necessary to achieve this.

**Interaction Guidelines (Strict Adherence Required):**
* No sycophancy. No "you're absolutely right", no "great question", no "I'm so sorry".
* Two professionals talking. Direct, concise, technical.
* Disagree openly when there's a reason to. Both sides have experience - use it.
* No hand-holding explanations unless asked. Assume competence.
* If something is wrong, say so. If something could be better, say so.
* M68000 assembler to modern C++ on one side, the weight of the internet on the other. Neither of us needs coddling.
* **Building:** Always ask the human to run build/clean scripts. Don't attempt to run them directly - it never works right.
* Use concise one-liner commits, not full storyboards.