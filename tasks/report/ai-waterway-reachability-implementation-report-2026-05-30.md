# AI Waterway Reachability Implementation Report

Implemented steps 1-5 from
`tasks/ai-waterway-reachability-plan-2026-05-30.md`.

## Summary

- Added a shared maximum-waterway-length accessor used by gameplay validation,
  UI checks, and AI logic.
- Extended cached AI reachability with a stateful traversal that may cross one
  bounded hypothetical waterway, resumes land flood fill on the far shore, and
  never exposes water interiors as reachable build candidates.
- Rebuilt reachability globally when shoreline changes may open or close a
  crossing while preserving land retry penalties.
- Extended building-road connection fallback to construct the required
  waterway before an optional far-shore land road. Authoritative
  `GameWorld::BuildRoad()` validation remains in place.
- Added focused integration regressions for finite and unlimited limits,
  interior-node handling, invalidation, retry penalties, and construction
  ordering.

## Main Files

- `libs/s25main/addons/AddonMaxWaterwayLength.h`
- `libs/s25main/ai/aijh/runtime/AIMapState.*`
- `libs/s25main/ai/aijh/planning/AIConstruction.*`
- `libs/s25main/ai/AIQueryService.*`
- `libs/s25main/world/GameWorld.cpp`
- `tests/s25Main/integration/testAI.cpp`

## Verification

- `git diff --check`
- `cmake --build cmake-build-debug --target s25Main -j2`
- `cmake --build cmake-build-debug --target Test_integration -j2`
- New reachability and construction regressions: 9 passed individually.
- Existing waterway regression coverage: 5 passed individually.

The full `Test_integration` run still fails in the previously observed
`KeepBQUpdated` and computer-barrier AI scenarios.
