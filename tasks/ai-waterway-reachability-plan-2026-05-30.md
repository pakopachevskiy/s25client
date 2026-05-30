# AI Waterway Reachability Implementation Plan

## Goal

Extend `AIMapState` reachability so the AI may select buildable land nodes that
can be connected to one of its flags through a boat road. Preserve the current
cheap pre-filter role of `Node::reachable`: final construction still has to run
the real road pathfinder and may reject a placement.

The algorithm must model a waterway as one indivisible segment. Land roads may
be split by intermediate flags, but water nodes must not reset the segment
length. A water traversal becomes useful only after it reaches another
flaggable shoreline node within the configured maximum waterway length.

## Current Behavior

- `AIMapState::InitReachableNodes()` seeds every AI-owned flag and
  `IterativeReachableNodeChecker()` flood-fills nodes accepted by
  `PathConditionRoad<GameWorldBase>(world, false)`.
- The `false` argument makes the cache land-only. Water-only nodes fail
  `GameWorldBase::IsRoadAvailable(false, pt)`.
- `Node::reachable` filters global and local building-position searches and
  resource-map calculations.
- Waterway construction support already exists separately:
  `AIQueryService::FindFreePathForNewWaterRoad()`,
  `AIConstruction::BuildWaterRoad()`, and
  `AIConstruction::BuildAlternativeWaterRoad()`.
- `GameWorld::BuildRoad()` centrally enforces the `MAX_WATERWAY_LENGTH` addon.
  Its values currently live in `addons/AddonMaxWaterwayLength.h` as
  `waterwayLengths = {3, 5, 9, 13, 21, 0}`, where `0` means unlimited.

## Design

Use a stateful breadth-first search instead of a single land-only queue.

Each queued item carries:

```cpp
struct ReachabilityState
{
    MapPoint pt;
    unsigned waterLength; // 0 while expanding land, >0 while inside one waterway
};
```

The expansion rules are:

1. In land mode (`waterLength == 0`), continue across nodes accepted by the
   existing land-road checker.
2. From a reachable flaggable shoreline node, allow entering an adjacent node
   accepted by `PathConditionRoad<GameWorldBase>(world, true)`. Queue that water
   node with `waterLength == 1`.
3. In water mode, continue only through nodes accepted by the water-road
   checker and increment `waterLength`.
4. Stop extending a water state when the configured finite maximum would be
   exceeded.
5. Allow a water state to exit onto a valid flaggable shoreline endpoint. Queue
   that endpoint in land mode and resume ordinary land expansion.
6. Never reset `waterLength` at an interior water node. This prevents the search
   from treating water roads as splittable by flags.

Only land nodes and valid shoreline endpoints should set
`AIMap::Node::reachable = true`. Water interiors are traversal states, not
building candidates, and must not become public reachability seeds.

Track visited state separately from `Node::reachable`:

- one visited bit for land mode;
- the shortest discovered in-water distance per node, or an equivalent
  `(node, waterLength)` state set where needed.

This avoids discarding a valid short water approach because a longer approach
visited the same node first. For the unlimited addon value, visited-state
deduplication must still prevent cycles.

## Implementation Steps

1. [x] Add a shared maximum-waterway-length accessor.
   - Move lookup of `waterwayLengths[ggs.getSelection(AddonId::MAX_WATERWAY_LENGTH)]`
     behind a small helper near `AddonMaxWaterwayLength`.
   - Preserve `0` as the unlimited sentinel.
   - Use the helper from `GameWorld::BuildRoad()`, the UI checks in
     `GameWorldView` and `dskGameInterface`, and the new AI reachability code so
     command validation and AI approximation cannot drift.

2. [x] Refactor `AIMapState` reachability into a stateful traversal.
   - Replace the plain `std::queue<MapPoint>` used by
     `IterativeReachableNodeChecker()` with explicit reachability states.
   - Keep the current land predicate unchanged for land expansion.
   - Add a water predicate using `PathConditionRoad<GameWorldBase>(owner_.gwb,
     true)`.
   - Add a narrow helper for valid waterway endpoints. It should match the
     engine's endpoint requirements closely enough for the cache: owned
     territory, shoreline terrain, usable building quality for a flag, and no
     nearby flag conflict unless the endpoint is already an owned flag.
   - Apply `failed_penalty` when committing a land or shoreline endpoint as
     reachable. Do not decrement it repeatedly while exploring water interiors.

3. [x] Make recalculation correct when a crossing opens or closes.
   - Separate one-time initialization of `failed_penalty` from recalculation of
     the `reachable` bits.
   - Start with a correctness-first full reachability rebuild from all owned
     flags when `UpdateReachableNodes()` is invoked. Local invalidation is risky
     once a changed shoreline node can add or remove reachability on a distant
     landmass.
   - Continue updating BQ, ownership, and border metadata only for the requested
     radius in `UpdateNodesAround()`.
   - Profile the full rebuild during AI turns. If it is material, add dirty
     component or shoreline caching as a follow-up optimization without
     changing traversal semantics.

4. [x] Keep construction validation authoritative.
   - Do not bypass `AIConstruction::ConnectFlagToRoadSytem()` or
     `GameWorld::BuildRoad()` checks after a water-reachable build position is
     selected.
   - Audit the build-job flow for a placement on the far shore. If the normal
     building-flag connection code only searches land roads, extend that flow
     deliberately so it can construct the required bounded waterway rather than
     repeatedly applying `failed_penalty`.
   - Keep worker and ordinary road-network connectivity land-only unless a
     caller explicitly needs ware-capable connectivity. Existing
     `IsConnectedToRoadSystem()` behavior should not silently change.

5. [x] Add focused integration tests in `tests/s25Main/integration/testAI.cpp`.
   - A buildable land node behind a short water gap changes from unreachable to
     reachable after AI initialization.
   - A water gap exactly equal to the configured maximum is accepted.
   - A gap one step longer than the configured maximum remains unreachable.
   - Unlimited mode accepts a gap longer than the largest finite setting.
   - A route cannot exceed the limit by resetting distance at interior water
     nodes.
   - A valid far-shore endpoint resumes land flood fill so buildable nodes
     beyond the shore become reachable.
   - Water interiors are not exposed as reachable build candidates.
   - Removing or blocking a shoreline endpoint and calling the update path
     clears stale far-shore reachability.
   - Existing land-only reachability and `failed_penalty` retry behavior remain
     covered.

6. [ ] Verify behavior and document the semantic change.
   - Update `docs/ai/road-route-selection.md` to explain that cached AI
     reachability may cross a bounded hypothetical waterway while worker
     connectivity remains land-only.
   - Run:

     ```sh
     git diff --check
     cmake --build cmake-build-debug --target s25Main -j2
     cmake --build cmake-build-debug --target Test_integration -j2
     ctest --test-dir cmake-build-debug --output-on-failure
     ```


## Likely Files

- `libs/s25main/addons/AddonMaxWaterwayLength.h`
- `libs/s25main/world/GameWorld.cpp`
- `libs/s25main/world/GameWorldView.cpp`
- `libs/s25main/desktops/dskGameInterface.cpp`
- `libs/s25main/ai/aijh/runtime/AIMapState.h`
- `libs/s25main/ai/aijh/runtime/AIMapState.cpp`
- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`
- `tests/s25Main/integration/testAI.cpp`
- `docs/ai/road-route-selection.md`

## Acceptance Criteria

- Buildable land on the far side of a valid waterway is included in
  `Node::reachable`.
- No path is accepted when any individual water segment exceeds the configured
  maximum length.
- Water interiors never act like intermediate flag locations.
- Unlimited mode terminates without cycling.
- Existing land placement behavior and authoritative road-construction checks
  remain intact.
