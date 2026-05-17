# Quantity Extractor Plan

## Goal

Add a new executable under `extras/quantity-extractor` that accepts a `.sav`
file, deserializes it into a game snapshot, and writes protobuf files describing
the map state relevant to player-territory building quality:

- `BuildingLocationsFile` from
  `external/proto-repo/building_locations.proto`
- `RoadLocationsFile` from `external/proto-repo/road_locations.proto`
- `EnvironmentSnapshotFile` from
  `external/proto-repo/environment_snapshot.proto`

Despite the executable name, the feature is about buildable territory and
building-quality context from `docs/gameplay/building-quality.md`: existing
buildings, construction sites, roads, trees, granite, and additional blocking
objects that explain why plots cannot host buildings.

## Open Schema Gap

`environment_snapshot.proto` currently contains only:

- `repeated Tree trees`
- `repeated Granite granite_rocks`

The requested "objects except trees and granite that prevent building in the
plot" cannot be represented without extending this schema. Add a backward
compatible field, for example:

```proto
message BlockingObject {
  uint32 x = 1;
  uint32 y = 2;
  uint32 object_id = 3;
  uint32 go_type = 4;
  uint32 nodal_object_type = 5;
  uint32 blocking_manner = 6;
  uint32 item_id = 7;
  uint32 item_file = 8;
  uint32 size = 9;
}

message EnvironmentSnapshotFile {
  string schema_version = 1;
  uint32 gameframe = 2;
  uint32 map_width = 3;
  uint32 map_height = 4;
  repeated Tree trees = 5;
  repeated Granite granite_rocks = 6;
  repeated BlockingObject blocking_objects = 7;
}
```

Keep trees and granite in their existing fields. Put every other node object
whose `GetBM()` is not `BlockingManner::None` into `blocking_objects`.

## Output Contract

Recommended CLI:

```text
quantity-extractor <save-file> [output-dir]
```

Default `output-dir` should be the current working directory. The tool should
write deterministic binary protobuf files:

```text
building_locations.pb
road_locations.pb
environment_snapshot.pb
```

Each file should set:

- `schema_version` to a tool-owned string such as `quantity-extractor/v1`.
- `gameframe` to `Savegame::start_gf`.
- `map_width` / `map_height` where the schema provides those fields.

Use binary protobuf serialization first. Add JSON/text output only if a later
consumer needs it.

## Build Integration

1. Add `extras/quantity-extractor/CMakeLists.txt`.
2. Add `add_subdirectory(quantity-extractor)` in `extras/CMakeLists.txt`.
3. Generate protobuf sources for the three snapshot schemas:
   - `building_locations.proto` plus `commons.proto`,
   - `road_locations.proto` plus `road_log.proto`,
   - `environment_snapshot.proto`.
4. Link the executable with:
   - `s25Main`,
   - `protobuf::libprotobuf`,
   - `Boost::filesystem`,
   - `Boost::nowide` if using nowide streams for portable paths.
5. Keep the executable C++17, matching `data-extractor`.

Prefer generating these proto files in the new executable target instead of
adding more generated snapshot-only sources to `s25Main`.

## Savegame Loading

Do not depend on `GameClient::inst()` in this tool. Implement a small local
`ILocalGameState` stub similar to `HeadlessLocalGameState` in
`extras/ai-battle/HeadlessGame.cpp`.

Load flow:

1. Initialize `RTTRCONFIG` and logging as other extras tools do.
2. Validate that the input exists, is a regular file, and has `.sav` extension.
3. Create `Savegame` and call `Load(path, SaveGameDataToLoad::All)`.
4. Convert every stored `BasePlayerInfo` into `PlayerInfo`.
5. Create a `Game` with `savegame.ggs`, `savegame.start_gf`, and those players.
   The current `data-extractor` uses default settings, but this tool should use
   the saved settings because addons and objectives can affect restored state.
6. Call `savegame.sgd.ReadSnapshot(game, localGameState)`.
7. Call `game.world_.InitAfterLoad()` before extracting building quality data.
   This matches the normal client/headless paths and makes recalculated
   building-quality and post-load world state available.

If loading fails, print `Savegame::GetLastErrorMsg()` when present and return a
non-zero exit code.

## Building Extraction

Source:

- `GameWorld::GetNumPlayers()`
- `GameWorld::GetPlayer(i)`
- `GamePlayer::GetBuildingRegister()`
- `BuildingRegister::GetBuildings(type)`
- `BuildingRegister::GetMilitaryBuildings()`
- `BuildingRegister::GetStorehouses()`
- `BuildingRegister::GetBuildingSites()`

Algorithm:

1. Create one `PlayerBuildingLocations` for every `GamePlayer::isUsed()`.
2. Store `player_id` as the engine player id. Confirm whether consumers expect
   zero-based ids, because `road_log.proto` comments use one-based ids.
   Document the chosen convention in the tool README.
3. For normal usual buildings, iterate every `BuildingType` for which
   `BuildingProperties::IsUsual(type)` and export
   `BuildingRegister::GetBuildings(type)`.
4. Export warehouses from `GetStorehouses()` and military buildings from
   `GetMilitaryBuildings()` because they are not returned by
   `GetBuildings(type)`.
5. Export harbor buildings only once. They appear in warehouse or usual lists
   depending on `BuildingProperties`; avoid also walking `GetHarbors()` unless
   it is used only as a sanity check.
6. For each building, fill:
   - `building_id` from `GetObjId()`,
   - `x` / `y` from `GetPos()`,
   - `created_gameframe` from `GetBuildStartingFrame()`,
   - `constructed_gameframe` from `GetBuildCompleteFrame()`.
7. For construction sites, group `GetBuildingSites()` by `GetBuildingType()`
   and fill:
   - `id` from `GetObjId()`,
   - `x` / `y` from `GetPos()`,
   - `created_gameframe` from `GetBuildStartingFrame()`.

Map `BuildingType` to proto `BuildingType` by ordinal only after verifying the
enum values still match `external/proto-repo/commons.proto`. Add a compile-time
or runtime assertion for representative values such as headquarters, fortress,
storehouse, and harbor building.

## Road Extraction

Source:

- `noFlag` / `noRoadNode` routes from the live node objects,
- `RoadSegment::GetF1()`, `GetF2()`, `GetLength()`, `GetRoute(i)`,
  `GetRoadType()`,
- `GameWorldBase::GetNO(pt)` while scanning map nodes.

Algorithm:

1. Scan every map point.
2. If the node object is a `noFlag`, inspect each route direction.
3. Insert each non-null `RoadSegment*` into a set to avoid duplicates, because
   every segment is reachable from both endpoints.
4. For every unique `RoadSegment`:
   - determine owner from one endpoint road node if possible; flags have a
     player owner, building-front access roads may end at a building,
   - start point is `GetF1()->GetPos()`,
   - end point is `GetF2()->GetPos()`,
   - route is every `GetRoute(i)` from `0` to `GetLength() - 1`,
   - type maps `RoadType::Normal`, `RoadType::Donkey`, `RoadType::Water` to
     `ROAD_LOG_TYPE_NORMAL`, `ROAD_LOG_TYPE_DONKEY`,
     `ROAD_LOG_TYPE_WATER`.
5. Group roads by player and road type.
6. Set `constructed_gameframe` to `0` initially, because `RoadSegment` does not
   currently expose a construction frame in the inspected API. If consumers need
   this, extend `RoadSegment` serialization in a separate change.

Map `Direction` to proto `RoadDirection` explicitly:

- `Direction::West` -> `ROAD_DIRECTION_WEST`
- `Direction::NorthWest` -> `ROAD_DIRECTION_NORTH_WEST`
- `Direction::NorthEast` -> `ROAD_DIRECTION_NORTH_EAST`
- `Direction::East` -> `ROAD_DIRECTION_EAST`
- `Direction::SouthEast` -> `ROAD_DIRECTION_SOUTH_EAST`
- `Direction::SouthWest` -> `ROAD_DIRECTION_SOUTH_WEST`

## Environment Extraction

Source:

- scan every map node,
- inspect `GetNO(pt)`,
- classify by `GO_Type`, `NodalObjectType`, and dynamic type.

Algorithm:

1. For every node object:
   - if it is `noTree`, append `Tree`;
   - else if it is `noGranite`, append `Granite`;
   - else if `obj->GetBM() != BlockingManner::None`, append
     `BlockingObject`.
2. For `Tree`, fill:
   - `x` / `y` from `noTree::GetPos()`,
   - `tree_type` from `GetTreeType()`,
   - `created_gameframe` as `0` unless tree creation frame is added later.
3. For `Granite`, fill:
   - `x` / `y` from the scanned point,
   - `granite_type` from `GetGraniteType()`,
   - `size` from `GetSize()` or `GetVisualSize()` after confirming consumer
     expectations,
   - `created_gameframe` and `updated_gameframe` as `0` unless provenance is
     added later.
4. For `BlockingObject`, fill generic fields:
   - `object_id` from `GetObjId()`,
   - `go_type` from `GetGOT()`,
   - `nodal_object_type` from `GetType()`,
   - `blocking_manner` from `GetBM()`,
   - for `noStaticObject` / `noEnvObject`, include `item_id`,
     `item_file`, and `size`.

This captures blockers such as buildings, building sites, flags, static map
objects, extensions, fires, charburner piles, ship building sites, signs, and
other non-tree/non-granite objects that can reduce building quality.

## Player Territory And Building Quality

The three requested output schemas do not have fields for per-player
`BuildingQuality` values. The extractor should still compute from the same
source of truth so output is aligned with `docs/gameplay/building-quality.md`:

- use `world.GetBQ(pt, playerId)` for player-facing quality,
- use `world.GetNode(pt).owner` to determine which player's territory a plot
  belongs to,
- use blocking objects only as explanatory environment context.

If downstream consumers need direct per-plot building quality, add a fourth
schema later instead of overloading the three requested files.

## Additional Data Required For Offline BQ Recalculation

The initial three-file set is not sufficient to calculate current
`BuildingQuality` for every player-owned plot, nor to recalculate it after a
building, flag, or road is hypothetically removed. `BQCalculator` and
`World::AdjustBQ()` need more than object locations:

- map geometry: width, height, and the same wrapped hex-neighbour convention as
  `MapBase`,
- per-node terrain build class for both stored terrain triangles (`t1`, `t2`),
  or enough terrain identity to derive each triangle's `TerrainBQ`,
- per-node altitude,
- per-node owner id, using the engine convention `0 = neutral`, `1..N =
  players`,
- per-node harbor marker (`harborId != 0`) so castle-quality points can become
  harbor-quality points,
- point-level road occupancy, or road segments that are complete enough to
  reconstruct `World::IsOnRoad(pt)`,
- object blocking manner for every node object, including flags, buildings,
  building extensions, construction sites, static objects, fires, fields, and
  other non-tree/non-granite blockers,
- enough object identity to remove one object and update the affected blocker
  and road occupancy state.

Add one of the following to the result set:

1. Preferred: a new `building_quality_inputs.proto` snapshot file containing
   one compact row-major record per map node:
   - `x` / `y` or row-major cell index,
   - `owner_id`,
   - `altitude`,
   - `terrain_bq_1`,
   - `terrain_bq_2`,
   - `harbor_id` or `has_harbor`,
   - `road_east`, `road_south_east`, `road_south_west` as `PointRoad` values,
   - `blocking_manner`,
   - `blocking_object_id` and `blocking_go_type`,
   - current raw `node.bq` and per-owner adjusted BQ as optional validation
     fields.
2. Alternative: reuse `player_plots.proto` for ownership and add a separate
   `map_terrain_snapshot.proto` for altitude, terrain BQ, harbor markers, and
   point roads. This keeps ownership reusable but makes consumers join two
   files before running the calculator.

Also extend `road_locations.proto` for hypothetical road removal:

- add `road_id`, taken from `RoadSegment::GetObjId()`,
- add endpoint object ids when available,
- keep full route directions from `f1` to `f2`,
- keep owner id and road type.

Without `road_id`, a consumer can remove a route geometrically, but cannot
unambiguously refer to "this road object" from another file or command.

Add explicit flag records. A flag is a blocking player object and road endpoint,
but it is not represented by `building_locations.proto`, and relying on generic
environment blockers makes road-removal simulations awkward. Either add a new
`flag_locations.proto` file or add a `flags` section to
`building_quality_inputs.proto` with:

- `flag_id`,
- `player_id`,
- `x` / `y`,
- `flag_type`,
- attached `road_id`s by direction.

With these additions, an offline consumer can:

1. reconstruct the raw `BQCalculator` inputs,
2. apply `World::AdjustBQ()` from ownership,
3. remove a building/flag/road by object id,
4. clear or update the affected blocker/road cells,
5. recalculate the same local neighbourhoods the engine recalculates:
   - object removal: affected point plus first and sometimes second neighbour
     ring,
   - road removal: road point plus `East`, `SouthEast`, and `SouthWest`
     neighbours for each changed road cell,
   - ownership changes, if modeled later: affected owned cells and their
     neighbours.

## Files To Add

Suggested structure:

```text
extras/quantity-extractor/
  CMakeLists.txt
  main.cpp
  SavegameLoader.h
  SavegameLoader.cpp
  QuantityExtractor.h
  QuantityExtractor.cpp
  ProtoWriters.h
  ProtoWriters.cpp
```

Responsibilities:

- `SavegameLoader`: owns CLI-independent savegame-to-`Game` hydration.
- `QuantityExtractor`: walks the hydrated `GameWorld` and builds in-memory
  protobuf messages.
- `ProtoWriters`: serializes protobuf messages to deterministic filenames and
  handles filesystem errors.
- `main.cpp`: argument parsing, validation, logging, and exit codes.

If `data-extractor` later needs the same savegame loader, move
`SavegameLoader` to a small shared helper target under `extras/common` rather
than duplicating more code.

## Tests

Add focused tests after the first implementation pass:

1. Unit-test enum mapping helpers for `BuildingType`, `RoadType`, `Direction`,
   `GO_Type`, and `BlockingManner`.
2. Add a savegame fixture or generate a tiny deterministic save in a test setup.
3. Run the extractor against that fixture and parse the three protobuf files
   back with generated classes.
4. Assert:
   - active players are present,
   - at least one known building location is exported with the expected type
     and position,
   - roads are deduplicated,
   - trees and granite remain in their existing fields,
   - a non-tree/non-granite blocking object appears in `blocking_objects`.

If a stable `.sav` fixture is too large for the repository, keep integration
coverage behind an existing test-data fixture and add unit tests around the
pure extraction functions.

## Documentation

Add `docs/tools/quantity-extractor.md` with:

- CLI syntax,
- output filenames,
- player id convention,
- schema versions,
- known zero-valued fields (`constructed_gameframe` for roads and environment
  creation/update frames until the engine tracks them),
- relationship to `docs/gameplay/building-quality.md`.

Optionally add the new tool to `docs/README.md` after implementation. Do not
edit the README in the planning-only change.

## Implementation Order

1. Extend `environment_snapshot.proto` with `BlockingObject`.
2. Add the new CMake target and generated protobuf integration.
3. Implement savegame loading with a local `ILocalGameState` and
   `InitAfterLoad()`.
4. Implement enum mapping helpers.
5. Implement building extraction.
6. Implement road extraction and deduplication.
7. Implement environment extraction, including blocker collection.
8. Implement protobuf writing and CLI.
9. Add tests.
10. Add tool documentation.

## Risks And Guardrails

- Do not infer building quality from raw `MapNode::bq` alone. Use
  `World::GetBQ(pt, player)` for player-facing quality because territory and
  front-flag ownership can downgrade a plot.
- Do not emit duplicate roads. Always deduplicate by `RoadSegment*` or object
  id.
- Keep `environment_snapshot.proto` changes backward compatible by adding new
  fields only.
- Be explicit about zero/default fields where the engine does not currently
  store creation frames.
- Avoid coupling the tool to `GameClient::inst()`. A CLI extractor should not
  require client UI/network singleton state.

## Acceptance Criteria

- `cmake --build build --target quantity-extractor` builds the new executable.
- `quantity-extractor path/to/save.sav out/` writes the three protobuf files.
- Files can be parsed by generated protobuf classes.
- Existing buildings and construction sites are grouped by player and building
  type.
- Roads are grouped by player and road type, with route directions preserved.
- Environment output includes trees, granite, and all other non-empty blocking
  objects except trees/granite in the new blocker field.
- The tool exits non-zero with useful errors for missing, invalid, or
  unsupported save files.
