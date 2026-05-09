# AGENTS.md

## Repository Overview

Return To The Roots (`s25client`) is an open-source C++14 rewrite of *The Settlers II*. The repository contains the main game client, the gameplay engine, platform drivers, auxiliary tools, tests, and project documentation. The game still requires original *Settlers II Gold Edition* assets at runtime; the build system creates a `put your S2-Installation in here` marker in the configured game-data directory.

The project is built with CMake and organized as a set of internal libraries under `libs/`, optional runtime plugins and tools under `extras/`, vendored dependencies under `external/`, and Boost.Test-based test suites under `tests/`.

## Build And Test

- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build --target s25client`
- Run tests: `ctest --test-dir build --output-on-failure`
- Useful CMake options:
  - `-DBUILD_TESTING=ON`
  - `-DRTTR_ENABLE_SANITIZERS=ON`
  - `-DRTTR_ENABLE_WERROR=ON`
  - `-DRTTR_ENABLE_COVERAGE=ON`

## Repository Structure

### Core source

- `libs/common/` shared utility code, math helpers, serialization, and low-level support types.
- `libs/driver/` driver interfaces for audio, video, input, and other platform-facing services.
- `libs/libGamedata/` asset and game-data loading abstractions, including Lua-backed data loading.
- `libs/libsamplerate/` internal samplerate wrapper/library integration.
- `libs/rttrConfig/` configuration and profile handling.
- `libs/s25client/` launcher/bootstrap layer for the client executable.
- `libs/s25main/` main gameplay engine and the largest code area: UI, world simulation, AI, networking, replay handling, map generation, Lua integration, and rendering support.

### Tools and plugins

- `extras/audioDrivers/` runtime audio driver plugins.
- `extras/videoDrivers/` runtime video driver plugins.
- `extras/ai-battle/` headless AI battle tooling and simulation support.
- `extras/data-extractor/` developer data extraction tool.
- `extras/macosLauncher/` macOS launcher packaging glue.

### Tests

- `tests/` Boost.Test suites mirroring the library layout.
- `tests/common/`, `tests/s25Main/`, `tests/s25client/`, `tests/libGameData/`, `tests/rttrConfig/` cover the corresponding production modules.
- `tests/testHelpers/` shared fixtures, helpers, and utilities for tests.
- `tests/testData/` checked-in fixtures and sample assets used by tests.

### Documentation and support files

- `docs/` canonical project documentation, grouped by architecture, gameplay, AI, Lua, development, and tools.
- `AI_COMBAT.md` AI combat tuning reference.
- `README.md` user-facing project introduction and build instructions.
- `CLAUDE.md` existing agent-oriented repository notes.
- `PACKAGING.md` packaging and distribution notes.
- `cmake/` custom CMake modules, platform configuration, and toolchains.
- `tools/` CI and release scripts.

### Dependencies and assets

- `external/` vendored or submodule-based third-party dependencies such as `libutil`, `libsiedler2`, `liblobby`, `kaguya`, and protocol definitions in `proto-repo`.
- `data/` packaged RTTR data files and supporting assets.
- `contrib/` locally provided third-party dependencies such as Boost copies used in some setups.

### Generated and local-only directories

- `build/`, `build-prof/`, `cmake-build-debug/` are generated build trees and should not be treated as source of truth.
- `tmp/` holds transient local outputs.
- `.codex/`, `.claude/`, `.idea/` are local tool/editor directories.

## Orientation Notes

- Start with `libs/s25main/` for gameplay behavior changes.
- Start with `libs/s25client/` for client startup, platform bootstrap, or executable wiring.
- Start with `extras/audioDrivers/` or `extras/videoDrivers/` for runtime driver/plugin work.
- Start with `tests/` for expected behavior and regression coverage.
- Start with `docs/README.md` when looking for deeper architecture or gameplay documentation.
