# AI Road Workload Debug Overlay Implementation Report

Implemented the road-workload debug-overlay plan.

## Summary

- Added an AI-owned, read-only `AIRoadWorkload` snapshot that scores owned
  flag-to-flag road segments by compatible ware-flow routes.
- Counted producer-to-consumer, producer-to-warehouse, and
  warehouse-to-consumer edges. Warehouses remain stable hubs regardless of
  stock, and eligible temporary consumers include construction sites, harbor
  expeditions, and military coin demand.
- Extended ware-mode road pathfinding to optionally return traversed road
  segments in travel order. Land roads, donkey roads, waterways, and road
  portions around ship connections are included; ship hops are omitted.
- Expanded cached segment scores onto non-flag interior road tiles and added
  the `Road Workload` AI debug overlay. Unused roads remain visible with score
  `0`, and selecting the overlay does not recalculate the snapshot.
- Refreshed the snapshot after AI startup and in the existing player-staggered
  `1500 GF` economic-maintenance cadence.
- Added the `CalculateRoadWorkload` runtime-profiler section with attempted
  pair-route count as work units and documented the workload model.
- Explicitly registered `AIRoadWorkload.cpp` in the `s25Main` CMake source list.
  The existing source glob did not use `CONFIGURE_DEPENDS`, so previously
  configured Ninja trees omitted the newly added implementation and failed at
  link time with undefined references.

## Main Files

- `libs/s25main/ai/aijh/runtime/AIRoadWorkload.*`
- `libs/s25main/pathfinding/RoadPathFinder.*`
- `libs/s25main/ai/AIQueryService.*`
- `libs/s25main/ai/AIInterface.h`
- `libs/s25main/ai/aijh/runtime/AIPlayerJH*`
- `libs/s25main/ai/aijh/debug/AIDebugView.h`
- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.*`
- `libs/s25main/ai/aijh/debug/AIPerfReporter.cpp`
- `libs/s25main/ingameWindows/iwAIDebug.cpp`
- `libs/s25main/CMakeLists.txt`
- `tests/s25Main/integration/testAI.cpp`
- `tests/s25Main/integration/testHarbor.cpp`
- `tests/s25Main/integration/testSeafaring.cpp`
- `docs/ai/wares-distribution.md`
- `docs/ai/performance-profiling.md`

## Verification

- `git diff --check`
- `cmake --build build --target s25client -j2`
- `cmake --build build --target Test_integration -j2`
- `cmake --build /home/pavel/repo/s25client/cmake-build-debug --target s25client -j 14`
- New and modified focused regressions: 8 passed individually.

The full `ctest --test-dir build --output-on-failure` run was attempted but
could not complete. `Test_integration` still reports the previously observed
`KeepBQUpdated` failures, and the later `Test_drivers` target hangs in this
environment.
