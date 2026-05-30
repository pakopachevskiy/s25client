# AI Waterway Road Implementation Report

Implemented the AI waterway-road plan from
`tasks/ai-waterway-road-plan-2026-05-30.md`.

## Summary

- Added waterway-specific AI route queries while keeping building and worker
  connectivity land-only.
- Added an AI planner for beneficial ware-logistics shortcuts between owned,
  land-connected shoreline flags.
- Enforced maximum waterway length centrally in `GameWorld::BuildRoad()` and
  prevented AI interior-flag placement and cleanup removal for waterways.
- Added a two-boat AI reserve policy that temporarily switches one shipyard to
  small-boat production when waterways need replenishment.
- Added integration regressions and documented the AI and gameplay policies.

## Main Files

- `libs/s25main/ai/AIQueryService.*`
- `libs/s25main/ai/aijh/planning/AIConstruction.*`
- `libs/s25main/ai/aijh/runtime/AIRoadController.cpp`
- `libs/s25main/ai/aijh/runtime/AIEconomyController.*`
- `libs/s25main/world/GameWorld.cpp`
- `tests/s25Main/integration/testAI.cpp`
- `tests/s25Main/integration/testGameCommands.cpp`
- `docs/ai/road-route-selection.md`
- `docs/gameplay/road-system.md`

## Verification

- `git diff --check`
- `cmake --build cmake-build-debug --target s25Main -j2`
- `cmake --build cmake-build-debug --target Test_integration -j2`
- Alternative-road integration suite: 4 passed.
- `GameCommandSuite`: 26 passed.
- New waterway regressions: 5 passed individually.
