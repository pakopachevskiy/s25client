# Building construction event logging plan

## Goal

Extend `building_log.csv` with three construction-site events:

- `builder_arrive`: emitted when a builder reaches a construction site.
- `board_deliver`: emitted when one board is delivered to a construction site.
- `stone_deliver`: emitted when one stone is delivered to a construction site.

These events should use the existing `BuildingEventLogger` CSV stream and keep the existing header:

```csv
gameframe,playerId,event,buildingType,buildingId,x,y
```

For construction-site events, `buildingId` should remain the construction site's object ID, matching the existing `construction_site_created` and `construction_site_cancelled` semantics documented in `docs/development/event-loggers.md`.

## Current code shape

- `libs/s25main/BuildingEventLogger.cpp` owns the shared CSV writer and maps typed logger functions to event-name strings.
- `libs/s25main/BuildingEventLogger.h` exposes the logger entry points used by gameplay code.
- `libs/s25main/buildings/noBuildingSite.cpp` is the right hook location for the new events:
  - `noBuildingSite::GotWorker(...)` is called when a planer or builder arrives.
  - `noBuildingSite::AddWare(...)` is called when an ordered construction ware is actually delivered.
- `libs/s25main/figures/nofBuilder.cpp` already logs the later `constructed` event when construction completes.

## Implementation steps

1. Add three logger functions to `BuildingEventLogger.h`:
   - `LogBuilderArrive(...)`
   - `LogBoardDeliver(...)`
   - `LogStoneDeliver(...)`

2. Implement those functions in `BuildingEventLogger.cpp` as thin wrappers around the existing internal `LogEvent(...)` helper:
   - `LogBuilderArrive(...)` writes event name `builder_arrive`.
   - `LogBoardDeliver(...)` writes event name `board_deliver`.
   - `LogStoneDeliver(...)` writes event name `stone_deliver`.

3. Include `BuildingEventLogger.h` from `libs/s25main/buildings/noBuildingSite.cpp`.

4. Log builder arrival in `noBuildingSite::GotWorker(...)` only on the builder path:
   - Keep planer handling unchanged.
   - After confirming `state != BuildingSiteState::Planing` and assigning `builder`, call `LogBuilderArrive(...)`.
   - Use `world->GetEvMgr().GetCurrentGF()`, `player`, `bldType_`, `GetObjId()`, `pos.x`, and `pos.y`.

5. Log material delivery in `noBuildingSite::AddWare(...)`:
   - For `GoodType::Boards`, after the ordered board is removed and `boards` is incremented, call `LogBoardDeliver(...)`.
   - For `GoodType::Stones`, after the ordered stone is removed and `stones` is incremented, call `LogStoneDeliver(...)`.
   - Do not log from `TakeWare(...)`; that function records a ware being assigned to the site, not delivered.
   - Do not log from `WareLost(...)`; failed deliveries should not produce delivery events.

6. Preserve existing inventory behavior:
   - Leave `DecreaseInventoryWare(...)` and `RemoveWare(...)` in their current order unless tests expose a reason to move them.
   - The new events are observational and should not affect ware counts, construction progress, or job assignment.

7. Update `docs/development/event-loggers.md`:
   - Add the three new event names under `BuildingEventLogger`.
   - Add hook notes for builder arrival and material delivery.
   - Clarify that `board_deliver` and `stone_deliver` are one row per delivered ware.

## Edge cases to handle deliberately

- Planer arrivals should not log `builder_arrive`.
- Board-only buildings should only produce `board_deliver` events.
- Stone events should only be emitted for actual `GoodType::Stones` deliveries.
- Harbor construction sites created from sea preload boards, stones, and a builder in the constructor. Do not log `board_deliver` or `stone_deliver` there unless the intended semantic is expanded from "delivered" to "present at site"; those materials are not delivered via `AddWare(...)`.
- If a sea-created harbor builder should count as an arrival, add an explicit `builder_arrive` log in the harbor-site constructor at site creation time. Otherwise, keep the event limited to `GotWorker(...)` arrivals.

## Verification plan

1. Build the touched target:

```sh
cmake --build build --target s25client
```

2. Run the existing test suite if the local build tree is available:

```sh
ctest --test-dir build --output-on-failure
```

3. Add or update focused tests only if there is an existing practical harness for event-log output. A useful regression should assert:
   - `builder_arrive` appears once when the builder reaches the construction site.
   - `board_deliver` appears once per delivered board.
   - `stone_deliver` appears once per delivered stone.
   - No delivery event is emitted when a ware is ordered and then lost.

4. Manually validate with event logging enabled through `extras/ai-battle` or another existing stats run:
   - Set a non-empty `STATS_CONFIG.statsPath`.
   - Enable the `building` logger if logger filtering is active.
   - Build a site requiring both boards and stones.
   - Confirm `building_log.csv` contains lifecycle rows plus the new arrival and delivery rows at the expected gameframes.
