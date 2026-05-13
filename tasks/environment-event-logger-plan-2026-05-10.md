# EnvironmentEventLogger Implementation Plan

## Goal

Add an `EnvironmentEventLogger` that records live surface resource objects:

- initial trees and granite already present on the map at gameframe `0`
- tree planting events with coordinates
- tree cut/removal events with coordinates
- granite hew events with coordinates

The logger should follow the existing `s25main` event logger conventions: gated by
`STATS_CONFIG.statsPath`, disabled by `--disable_event_logging`, selectable through
`--enabled_event_loggers`, buffered in memory, flushed on 500-gameframe boundaries,
and flushed again at shutdown.

## Existing Context

Relevant docs:

- `docs/development/event-loggers.md`
- `docs/development/map-file-parsing-and-minimap-rendering.md`

Relevant code:

- `libs/s25main/ai/aijh/debug/StatsConfig.h`
- `libs/s25main/EventLogBatchWriter.h`
- `libs/s25main/RoadEventLogger.*`
- `libs/s25main/CountryPlotEventLogger.*`
- `libs/s25main/world/MapLoader.cpp`
- `libs/s25main/nodeObjs/noTree.*`
- `libs/s25main/nodeObjs/noGranite.*`
- `libs/s25main/figures/nofForester.cpp`
- `libs/s25main/figures/nofWoodcutter.cpp`
- `libs/s25main/figures/nofStonemason.cpp`

`MapLoader::PlaceObjects(...)` imports initial trees from `ObjectType` values
`0xC4`, `0xC5`, and `0xC6`, and granite from `0xCC` and `0xCD`. Runtime minimap
rendering reads `NodalObjectType::Tree` and `NodalObjectType::Granite` from the
live world, so the logger should also use live world state rather than raw map
layers for its initial snapshot.

## Output Format

Use a length-delimited protobuf stream named `environment_log.pb`.

Reasons:

- initial map snapshots can contain many objects, and protobuf keeps the output
  compact
- several current structured loggers already use protobuf streams with an
  initial header
- protobuf can represent event-specific metadata without ambiguous CSV columns

Add `external/proto-repo/environment_log.proto`:

```proto
syntax = "proto3";

package ru.pkopachevsky.proto;

option java_multiple_files = true;
option java_package = "ru.pkopachevsky.proto.environmentlog";
option go_package = "s2operator/v1";

message EnvironmentLogHeader {
  uint32 map_width = 1;
  uint32 map_height = 2;
}

enum EnvironmentEventType {
  ENVIRONMENT_EVENT_TYPE_UNSPECIFIED = 0;
  ENVIRONMENT_EVENT_TYPE_TREE_INITIAL = 1;
  ENVIRONMENT_EVENT_TYPE_GRANITE_INITIAL = 2;
  ENVIRONMENT_EVENT_TYPE_TREE_PLANTED = 3;
  ENVIRONMENT_EVENT_TYPE_TREE_CUT = 4;
  ENVIRONMENT_EVENT_TYPE_GRANITE_HEW = 5;
}

message EnvironmentLogRecord {
  uint32 gameframe = 1;
  EnvironmentEventType event_type = 2;
  uint32 x = 3;
  uint32 y = 4;

  optional uint32 tree_type = 5;
  optional uint32 granite_type = 6;
  optional uint32 granite_size_before = 7;
  optional uint32 granite_size_after = 8;
}
```

Notes:

- `TREE_INITIAL` and `GRANITE_INITIAL` distinguish initial trees from initial
  granite without a separate object-type field.
- `GRANITE_HEW` records size before and after. If the hew removes the final
  granite object, write `granite_size_after = 0`; consumers can treat that as
  depletion.
- `tree_type` requires a public const accessor on `noTree`.
- `granite_type` requires a public const accessor on `noGranite`.

## Implementation Steps

1. Add logger type and CLI name.

   - Add `Environment` to `EventLoggerType` in
     `libs/s25main/ai/aijh/debug/StatsConfig.h`.
   - Add CLI name `environment`.
   - Update `GetSupportedEventLoggerNames()`.
   - Update `TryParseEventLoggerType(...)` to accept `environment`,
     `environments`, and `environmenteventlogger`.
   - Update docs in `docs/development/event-loggers.md` and
     `docs/tools/ai-battle.md`.

2. Add the protobuf schema and generated-code wiring.

   - Add `external/proto-repo/environment_log.proto`.
   - Update any proto module or build list if this repository requires explicit
     registration.
   - Regenerate protobuf outputs using the repository's existing proto
     generation workflow.
   - Verify `environment_log.pb.h` is available to `libs/s25main`.

3. Add `EnvironmentEventLogger.{h,cpp}` in `libs/s25main`.

   Suggested public API:

   ```cpp
   namespace EnvironmentEventLogger {
   void LogInitialEnvironment(unsigned gf, const GameWorldBase& world);
   void LogTreePlanted(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree);
   void LogTreeCut(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree);
   void LogGraniteHew(unsigned gf, const GameWorldBase& world, MapPoint pt, const noGranite& granite,
                      unsigned char sizeBefore, unsigned char sizeAfter);
   }
   ```

   Implementation should mirror `RoadEventLogger.cpp` / `CountryPlotEventLogger.cpp`:

   - keep `HeaderInfo` with map width and height
   - write one `EnvironmentLogHeader` before the first record
   - buffer `EnvironmentLogRecord` values
   - flush every 500 gameframes
   - flush pending records from a static destructor
   - short-circuit every public function with
     `STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment)`

4. Add object metadata accessors.

   In `noTree.h`:

   - `unsigned char GetTreeType() const`

   In `noGranite.h`:

   - `GraniteType GetGraniteType() const`

   Keep these read-only and avoid exposing mutable state.

5. Log the initial snapshot.

   Add `#include "EnvironmentEventLogger.h"` to `MapLoader.cpp`.

   After `PlaceObjects(map)` succeeds and before later systems mutate map
   objects, call:

   ```cpp
   if(world_.GetEvMgr().GetCurrentGF() == 0)
       EnvironmentEventLogger::LogInitialEnvironment(0, world_);
   ```

   `LogInitialEnvironment(...)` should scan `world.GetSize()` using
   `RTTR_FOREACH_PT`, inspect `world.GetNO(pt)->GetType()`, and enqueue:

   - `TREE_INITIAL` for `NodalObjectType::Tree`
   - `GRANITE_INITIAL` for `NodalObjectType::Granite`

   Do not read raw `ObjectType` / `ObjectIndex` layers here. The live object
   scan handles any future map-loader normalization and matches ingame minimap
   behavior.

6. Log tree planting.

   In `nofForester::WorkFinished()`:

   - after creating the new `noTree`
   - before or after `world->SetNO(...)`, keep a reference or pointer to the
     new object
   - call `EnvironmentEventLogger::LogTreePlanted(world->GetEvMgr().GetCurrentGF(), *world, pos, *tree)`

   Example shape:

   ```cpp
   auto* tree = new noTree(pos, RANDOM_ELEMENT(AVAILABLE_TREES[landscapeType]), 0);
   world->SetNO(pos, tree);
   EnvironmentEventLogger::LogTreePlanted(world->GetEvMgr().GetCurrentGF(), *world, pos, *tree);
   ```

7. Log tree cut/removal.

   Best hook: `noTree::HandleEvent(...)`, state `FallingFallen`, immediately
   before `GetEvMgr().AddToKillList(this)` and `world->SetNO(...)`.

   Rationale:

   - `nofWoodcutter::WorkStarted()` only schedules the fall and may be aborted.
   - `nofWoodcutter::WorkFinished()` gives the worker wood but the tree still
     remains in falling animation state.
   - `FallingFallen` is the point where the tree is actually removed from the
     live surface object map.

   Add `#include "EnvironmentEventLogger.h"` to `noTree.cpp` and call:

   ```cpp
   EnvironmentEventLogger::LogTreeCut(world->GetEvMgr().GetCurrentGF(), *world, pos, *this);
   ```

8. Log granite hewing.

   Best hook: `nofStonemason::WorkFinished()`.

   - Capture `noGranite& granite = *world->GetSpecObj<noGranite>(pos)`.
   - Capture `sizeBefore = granite.GetSize()`.
   - If `granite.IsSmall()`, log `sizeAfter = 0` before `world->DestroyNO(pos)`.
   - Otherwise call `granite.Hew()`, then capture `sizeAfter = granite.GetSize()`.
   - Call `EnvironmentEventLogger::LogGraniteHew(...)`.

   This hook sees both shrinking and removal in one place and has access to the
   coordinates already used by minimap updates.

9. Add the new source to the build.

   - Add `EnvironmentEventLogger.cpp` to `libs/s25main/CMakeLists.txt`.
   - Add the header if the local CMake list explicitly enumerates headers.

10. Add tests.

    Minimum focused tests:

    - logger-name parsing accepts `environment` and
      `EnvironmentEventLogger`
    - initial snapshot emits records for placed `noTree` and `noGranite`
    - `nofForester::WorkFinished()` emits `TREE_PLANTED`
    - tree removal emits `TREE_CUT` only when the tree reaches actual removal,
      not when `FallSoon()` is scheduled
    - stonemason work emits `GRANITE_HEW` with expected coordinate and size
      delta, including `granite_size_after = 0` for the final depletion case

    If direct worker tests are expensive, add lower-level logger tests plus one
    integration test that drives the relevant world object methods.

11. Update documentation.

    In `docs/development/event-loggers.md`, add:

    - purpose
    - hooks
    - output file
    - schema path
    - initial snapshot semantics
    - event semantics for planted/cut/hew and granite depletion via
      `granite_size_after = 0`

    In `docs/tools/ai-battle.md`, include `environment` in the supported
    `--enabled_event_loggers` names.

12. Verify.

    Run:

    ```sh
    cmake --build build --target s25client
    ctest --test-dir build --output-on-failure
    ```

    If protobuf regeneration changes generated files, also run the repository's
    proto-specific generation/check target if one exists.

## Edge Cases To Decide Before Coding

- Whether tree cut should mean "woodcutter completed work" or "tree object
  disappeared from the map." This plan uses disappearance from the live object
  map because the user asked to track surface objects on the map.
- Whether initial snapshot should include non-wood-producing tree type `5`.
  This plan includes all `NodalObjectType::Tree` objects because the logger is
  about surface objects, not wood production.
