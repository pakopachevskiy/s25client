# AI Waterway Road Implementation Plan

## Goal

Allow AI players to build useful waterways between owned shoreline flags without
changing the land-road rules used to connect buildings and workers to the road
system. Waterways are ware-logistics shortcuts: people cannot walk over them,
and each completed segment needs a helper plus a boat.

## Implementation Steps

1. [x] Add waterway-specific route queries.
   - Add `AIQueryService::FindFreePathForNewWaterRoad()` and expose it through
     `AIInterface`.
   - Use the ordinary free-terrain pathfinder with
     `makePathConditionRoad(gwb, true)`, matching the human waterway builder.
   - Do not reuse the alternating land-road pathfinder, BQ-weighted refinement,
     interior-flag spacing checks, or `GetMaxNonFlaggableRun()`.
   - Keep `FindFreePathForNewRoad()`, `FindPathOnRoads()`,
     `IsConnectedToRoadSystem()`, and `AIMapState::reachable` land-only.

2. [x] Add a dedicated AI waterway-shortcut planner.
   - Add `AIConstruction::BuildAlternativeWaterRoad()` and call it after the
     normal secondary-road attempt for suitable non-military building flags,
     especially storehouses and harbors.
   - Search nearby owned flags, require both endpoints to remain land-connected,
     and find a water-only route between shoreline flags.
   - Score the candidate against the existing ware-capable road-network route.
     Build only when it is a meaningful shortcut and a reachable warehouse can
     provide `Job::Helper` plus `GoodType::Boat`.
   - Extend `AIConstruction::BuildRoad()` or add a narrow helper so accepted
     candidates emit `aii.BuildRoad(start, true, route)`.

3. [x] Make waterway construction obey engine-level invariants.
   - In `AIRoadController::HandleRoadConstructionComplete()`, return before
     calling `SetFlagsAlongRoad()` when the completed segment has
     `RoadType::Water`.
   - Move enforcement of the `MAX_WATERWAY_LENGTH` addon into
     `GameWorld::BuildRoad()` so AI, network, replay, and UI-issued commands use
     the same limit. Keep the existing UI cap as early feedback.
   - Audit `AIRoadController::RemoveUnusedRoad()` so useful waterway shortcuts
     are not immediately removed as redundant circles.

4. [x] Add AI boat-reserve management.
   - Add a small-boat reserve policy in the AI economy or event controller.
   - When waterways exist or are planned and stored boats fall below the reserve,
     switch at least one shipyard to `nobShipYard::Mode::Boats` and enable
     production.
   - Once the reserve is restored, allow shipyards to return to large-ship
     production according to the existing ship policy. This is required for
     `StartWares::VLow`, which starts with zero boats.

5. [x] Add regression tests and documentation.
   - Add integration tests covering: a beneficial waterway emits
     `BuildRoad(..., true, ...)`; primary building connectivity remains
     land-only; completed waterways do not queue interior flags; unavailable
     boats prevent construction; boat reserve management switches a shipyard to
     boat mode; and the maximum-waterway-length addon is enforced centrally.
   - Add a cleanup regression test proving a useful waterway survives unused-road
     pruning.
   - Update `docs/ai/road-route-selection.md` and
     `docs/gameplay/road-system.md` with the AI waterway-shortcut policy.

## Acceptance Criteria

- AI players construct waterways only as ware-logistics shortcuts between owned,
  land-connected shoreline flags.
- Worker and building connectivity logic remains land-only.
- Waterways receive no AI-added interior flags and respect the configured
  maximum length regardless of command source.
- The AI can replenish boats instead of exhausting the initial HQ stock.
- Focused integration tests cover route selection, construction, completion,
  supply management, cleanup, and addon enforcement.
