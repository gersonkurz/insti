# insti - Claude Code Instructions

## Project Summary
- Windows-only utility for managing, snapshotting, and restoring application state (files, registry, services, environment variables). Enables atomic context switching between customer configurations/versions.
- Detailed code description can be found in `overview.md`

## Tech Stack
- **Language:** C++23, statically linked, zero external dependencies
- **Architecture:** x64, Win32 and ARM64
- **UI:** Dear ImGui (Win32/DirectX)
- **Config:** TOML (pluggable format readers)
- **Archive:** Zip

## Build Structure
- `shared/` - Static library (core engine)
- `insti/` - CLI executable
- `instinctiv/` - GUI executable (Phase 4)
- No DLLs

## Development Rules

### Communication Style
- Direct, concise, technical - two professionals talking
- No sycophancy, no hand-holding explanations
- Disagree openly when warranted
- If something is wrong or could be better, say so
- Concise one-liner commits, not storyboards
