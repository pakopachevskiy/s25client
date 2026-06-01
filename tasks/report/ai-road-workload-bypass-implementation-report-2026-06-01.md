# AI Road Workload Bypass Implementation Report

Implemented the global AI road-workload bypass activation requested from
`docs/ai/road-route-selection.md` and `docs/development/road-workload.md`.

## Summary

- Extended `AIRoadWorkload` with segment-level records containing endpoint
  flags, workload score, road length, and water-road marker.
- Added sorted hot-segment lookup so AI code can inspect high-workload road
  segments without re-reading debug-overlay tile values.
- Added a road-network query that can find paths while avoiding one existing
  road segment.
- Added `AIConstruction::BuildAlternativeRoadBypassingSegment()` for bounded
  land-road shortcut construction around a hot segment.
- The bypass builder searches owned flags near both hot-segment endpoints,
  requires the current road path to cross the hot segment, rejects overly long
  or weak free-terrain roads, applies the existing road-route BQ penalty and
  optional weighted refinement, and skips builds when an adequate existing
  bypass already exists.
- Added a player-staggered global activation in `AIPlayerJH::RunGF()` every
  `2500 GF`, using workload threshold `600`, checking at most 8 hot land
  segments per pass, and building at most one bypass per activation.
- Added a `CheckRoadWorkloadHotspots` runtime-profiler section and CSV output.
- Updated AI route-selection, road-workload, and performance-profiling docs.
- Added integration coverage for segment hot-spot exposure and the new bypass
  construction path.

## Main Files

- `libs/s25main/ai/aijh/RoadWorkloadSegment.h`
- `libs/s25main/ai/aijh/runtime/AIRoadWorkload.*`
- `libs/s25main/ai/AIQueryService.*`
- `libs/s25main/ai/AIInterface.h`
- `libs/s25main/ai/aijh/planning/AIConstruction.*`
- `libs/s25main/ai/aijh/runtime/AIPlayerJH*`
- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.*`
- `libs/s25main/ai/aijh/debug/AIPerfReporter.cpp`
- `libs/s25main/CMakeLists.txt`
- `tests/s25Main/integration/testAI.cpp`
- `docs/ai/road-route-selection.md`
- `docs/development/road-workload.md`
- `docs/ai/performance-profiling.md`

## Verification

- `git diff --check`
- `cmake --build build --target Test_integration`
- `build/bin/Test_integration --run_test=AI/RoadWorkload_AccumulatesWareEdgesAndRefreshesDisabledProducer --catch_system_errors=no`
- `build/bin/Test_integration --run_test=AI/BuildAlternativeRoadBypassingSegment_BuildsShortcutAroundHotSegment --catch_system_errors=no`

The focused build and regressions passed. The build still emits existing
`-Wsuggest-override` warnings from `AIPlayerJH.h`.

The broader `ctest --test-dir build --output-on-failure` run was attempted but
did not complete cleanly in this environment. `Test_integration` reported the
previously observed `AI/KeepBQUpdated` failures plus two military barrier AI
test failures, then the run stopped making progress around `Test_drivers` and
was terminated.
