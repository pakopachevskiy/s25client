# Plan: Proposal 1 - Cache Best Positions Per Building Type

## Goal

Avoid repeated full-map scans in
`AIJH::GlobalPositionFinder::FindBestPosition(BuildingType)` by caching the
last computed answer per `BuildingType` and recomputing only after relevant AI
world state changes.

This is intentionally scoped to proposal 1 only. It does not change the
scoring algorithm, proximity implementation, or job retry policy yet.

## Current Integration Points

The current global search path is:

- `BuildJob::TryToBuild()`
- `AIPlanningContext::FindBestPosition(BuildingType)`
- `AIEconomyController::FindBestPosition(BuildingType)`
- `GlobalPositionFinder::FindBestPosition(BuildingType)`

`GlobalPositionFinder` is already long-lived because it is owned by
`AIPlayerJH`, so it is a good place to keep a per-building-type cache.

The search depends on more than static map terrain. A cached answer becomes
stale when any of these inputs change:

- `AINode` state used in the main scan:
  - `reachable`
  - `owned`
  - `farmed`
  - `bq`
- harbor exclusion state
- proximity/rating inputs based on existing buildings and building sites
- dynamic building counts from `BuildingPlanner::GetNumBuildings(type)`

## Recommended Design

### 1. Add a coarse generation-based cache inside `GlobalPositionFinder`

Add a cache entry per `BuildingType`, for example:

```cpp
struct CachedBestPosition
{
    uint32_t generation = 0;
    bool computed = false;
    MapPoint point = MapPoint::Invalid();
    int rating = 0;
};
```

Add to `GlobalPositionFinder`:

- `helpers::EnumArray<CachedBestPosition, BuildingType> cache_`
- `uint32_t cacheGeneration_ = 1`
- `void InvalidateCache()`

Behavior:

- `FindBestPosition(bt)` first checks `cache_[bt]`
- if `computed && generation == cacheGeneration_`, return cached `point`
- otherwise run the current full scan, then store the result for that type

Cache `MapPoint::Invalid()` too. Repeated failed lookups are part of the
observed cost, so negative results should be memoized for the same generation.

### 2. Start with full-cache invalidation, not selective invalidation

Even though the cache is per `BuildingType`, the first implementation should
invalidate the whole cache by bumping one generation counter.

Reason:

- the result depends on shared map state
- proximity and rating rules cross building-type boundaries
- missing one invalidation hook is worse than over-invalidating

This keeps proposal 1 small and low-risk. If cache hit rate is still poor after
profiling, a later iteration can split the generation into narrower domains.

## Invalidation Plan

### 3. Add one explicit invalidation API on the AI owner side

Add a small forwarding method on `AIPlayerJH`, for example:

- `void InvalidateGlobalPositionCache();`

Implementation:

- delegate to `globalPositionFinder->InvalidateCache()`

This gives all runtime code a single obvious hook instead of letting random
callers reach into `GlobalPositionFinder` directly.

### 4. Invalidate on map-state updates that change candidate eligibility

Hook invalidation into places that mutate `AINode` data used by the global
scan:

- `AIMapState::UpdateNodesAround(...)`
- `AIMapState::SetFarmedNodes(...)`
- `AIMapState::RefreshBuildingQualities()`

Notes:

- `RefreshBuildingQualities()` should invalidate only if
  `nodesWithOutdatedBQ_` was non-empty and at least one node was refreshed
- `UpdateNodesAround(...)` already covers ownership, reachable-node refresh,
  border-adjacent BQ changes, and general building-quality changes near local
  world updates

### 5. Cover direct `AINode` mutations that bypass `AIMapState`

There are a few direct writes today that would otherwise bypass the cache
invalidation path:

- `AIEventHandler::HandleTreeChopped()`
  - sets `GetAINode(pt).reachable = true`
- `BuildJob::BuildMainRoad()`
  - sets `GetAINode(target).bq = bq`
  - sets `GetAINode(target).reachable = false`
  - sets `failed_penalty`

Plan:

- either add explicit `InvalidateGlobalPositionCache()` calls next to those
  direct mutations
- or, better, replace the direct writes with small helper methods on
  `AIPlayerJH` / `AIMapState` so node mutations always go through one path

The second option is cleaner and reduces the chance of future misses.

### 6. Invalidate on construction-state changes that affect proximity/counts

The cached answer also depends on surrounding buildings/building sites and on
`BuildingPlanner::GetNumBuildings(type)`.

Add invalidation at these points:

- `AIConstruction::ConstructionOrdered(const BuildJob&)`
  - a newly ordered site changes effective availability immediately from the
    AI's point of view, even before the next NWF
- `AIEventHandler::HandleBuildingDestroyed(...)`
- `AIEventHandler::HandleBuildingFinished(...)`
- `AIEventHandler::HandleNewMilitaryBuildingOccupied(...)`
- `AIEventHandler::HandleMilitaryBuildingLost(...)`
- `AIEventHandler::HandleNoMoreResourcesReachable(...)`

Rationale:

- proximity queries look at existing buildings / sites
- building counts influence `CheckProximity(...)`
- global jobs can run multiple times before the planner refresh naturally

This is intentionally conservative. It is acceptable if some events invalidate
the cache a bit more often than strictly necessary.

### 7. Invalidate on harbor-relevant world changes

Proposal 1 explicitly calls out harbor availability as an invalidation source.
At minimum, invalidate on:

- harbor building finished
- harbor building destroyed
- new colony founded
- lost land / border changes near harbor-accessible territory

Practical first step:

- piggyback on the existing broad invalidations from
  `HandleBuildingFinished`, `HandleBuildingDestroyed`,
  `HandleBorderChanged`, and `HandleLostLand`

That is probably enough for the first version.

## Implementation Steps

### Phase 1: Cache container and lookup path [done]

Files:

- `libs/s25main/ai/aijh/planning/GlobalPositionFinder.h`
- `libs/s25main/ai/aijh/planning/GlobalPositionFinder.cpp`

Work:

- add cache entry struct and generation counter
- add `InvalidateCache()`
- wrap existing `FindBestPosition(bt)` logic with cache lookup/store
- keep the current scan implementation unchanged for misses

### Phase 2: Owner-level invalidation API [done]

Files:

- `libs/s25main/ai/aijh/runtime/AIPlayerJH.h`
- `libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp`

Work:

- add `InvalidateGlobalPositionCache()`
- delegate to `GlobalPositionFinder`

### Phase 3: Wire invalidation into map-state updates [done]

Files:

- `libs/s25main/ai/aijh/runtime/AIMapState.cpp`
- `libs/s25main/ai/aijh/runtime/AIPlayerJHMapState.cpp`

Work:

- call `owner_.InvalidateGlobalPositionCache()` from:
  - `UpdateNodesAround`
  - `SetFarmedNodes`
  - `RefreshBuildingQualities` when it performs work

### Phase 4: Wire invalidation into construction/event paths [done]

Files:

- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`
- `libs/s25main/ai/aijh/planning/Jobs.cpp`
- `libs/s25main/ai/aijh/runtime/AIEventHandler.cpp`

Work:

- invalidate when construction is ordered
- invalidate after building lifecycle events that can affect proximity,
  harbor exclusion, or map ownership/reachability
- invalidate at remaining direct `AINode` mutation sites

### Phase 5: Add lightweight instrumentation [done]

Files:

- `libs/s25main/ai/aijh/planning/GlobalPositionFinder.*`
- optionally `libs/s25main/ai/aijh/debug/*` if there is already a suitable
  stats sink

Work:

- count cache hits / misses per `BuildingType`
- optionally count cached negative-result hits
- keep it simple; temporary debug logging is enough if a permanent stats path
  is too invasive

## Validation

### Functional validation

Check that global build jobs still choose valid build points after:

- road construction failures
- tree chopping / terrain opening up
- border changes
- harbor construction or destruction
- farm / charburner placement and removal
- repeated global job retries in an otherwise unchanged world

Important edge case:

- a cached answer must not survive a failed build attempt that marks the node
  unreachable or updates its `bq`

### Performance validation

Repeat the same workload used in the original profiling note and compare:

- total `FindBestPosition()` call count
- cache hit rate per `BuildingType`
- wall-clock time / GF/sec
- total time spent in `ExecuteGlobalBuildJobs`

Expected result:

- a large drop in repeated full-map scans for the same type
- especially visible for global jobs that are retried several times in a short
  period

## Risks And Guardrails

### Main risk: stale cache due to missed invalidation

Mitigation:

- start with one coarse generation for all building types
- over-invalidate rather than under-invalidate
- centralize invalidation behind `AIPlayerJH::InvalidateGlobalPositionCache()`
- eliminate or isolate direct `AINode` mutations

### Secondary risk: low hit rate because invalidation is too broad

That is acceptable for the first pass. Proposal 1 is still worthwhile if it
removes bursts of repeated identical scans between real world changes.

If hit rate remains low after measurement, the next refinement should be:

- split invalidation sources into separate generations, for example:
  - node eligibility generation
  - construction/proximity generation
  - harbor generation

## Recommended Scope Cut

For the first implementation, stop after:

1. generation-based caching in `GlobalPositionFinder`
2. coarse invalidation hooks in `AIMapState`, `AIConstruction`, `Jobs`, and
   `AIEventHandler`
3. basic hit/miss instrumentation

Do not mix in:

- resource-map caching
- shortlist search
- job retry throttling
- spatial indexing

Those are separate proposals and should be measured independently.
