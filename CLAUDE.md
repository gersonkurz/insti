# insti - Claude Code Instructions

## On Startup
Read both `overview.md` (original specification) and `implementation-plan.md` (current plan). Follow the plan.

## Project Summary
Windows-only utility for managing, snapshotting, and restoring application state (files, registry, services, environment variables). Enables atomic context switching between customer configurations/versions.

## Tech Stack
- **Language:** C++20, statically linked, zero external dependencies
- **Architecture:** x64 and ARM64
- **UI:** Dear ImGui (Win32/DirectX) - Phase 4
- **Config:** TOML (pluggable format readers)
- **Archive:** Zip

## Core Concepts
- **Blueprint:** In-memory representation of the recipe. Owns variable resolution.
- **Blueprint file:** Serialized blueprint (TOML, JSON, etc.). Lives inside snapshot.
- **Snapshot:** Zip archive containing blueprint file + artifacts.
- **Instance:** Deployed state on a machine.
- **Resources:** Bidirectional (files, registry, service, environment) - backup/restore/clean.
- **Hooks:** Phase-specific (process, kill, config) - PreBackup, PostBackup, PreRestore, PostRestore, PreClean, PostClean.
- **Variables:** Bidirectional substitution - auto-detect on backup, resolve on restore.

## Build Structure
- `shared/` - Static library (core engine)
- `insti/` - CLI executable
- `instinctiv/` - GUI executable (Phase 4)
- No DLLs

## Development Rules

### Process
- Follow `implementation-plan.md`
- Currently in Phase 1 (POC): files resource only, built-in variables, backup/restore/clean, minimal CLI
- Milestone-based with verifiable artifacts
- **ALWAYS discuss proposals before implementing** - explain what you plan to do, get approval, then implement

### Building
- **Never run build/clean scripts directly** - always ask the human to execute them

### Communication Style
- Direct, concise, technical - two professionals talking
- No sycophancy, no hand-holding explanations
- Disagree openly when warranted
- If something is wrong or could be better, say so
- Concise one-liner commits, not storyboards

## Reference
- Original specification: `overview.md`
- Current plan: `implementation-plan.md`
