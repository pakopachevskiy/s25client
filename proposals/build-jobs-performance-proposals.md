# Build Jobs Performance Proposals

This note summarizes the current construction-job profiling results and
proposes concrete ways to make the build-jobs algorithm more performant.

It is broader than the existing `GlobalPositionFinder` notes in
`proposals/`. Those files focus on the global-search cache itself. This
document covers the whole construction path:

- global build jobs
- normal build jobs
- connect jobs
- road-optimization follow-up work

## Findings

The relevant profiler output is in `tmp/performance_result.txt`.

The dominant AI cost is construction work:

- `ExecuteAIJob`: `37548.558 ms`
- `ExecuteConstructionJobs`: `36711.834 ms`
- `ExecuteGlobalBuildJobs`: `20044.517 ms`
- `ExecuteConnectJobs`: `9063.694 ms`
- `ExecuteBuildJobs`: `7600.129 ms`

Within global build jobs, the most expensive building types are:

- `Woodcutter`: `6008.944 ms`
- `Fishery`: `4199.641 ms`
- `Farm`: `2274.183 ms`
- `Coal mine`: `1426.588 ms`
- `Storehouse`: `1166.993 ms`

That means most of the opportunity is in:

1. reducing repeated full-map global searches
2. reducing repeated pathfinding in road connection work
3. reducing repeated linear scans over buildings and building sites

## Current Cost Drivers

### 1. Global jobs still do full-map scans

`BuildJob::TryToBuild()` calls `FindBestPosition(type)` for global jobs.
`GlobalPositionFinder::FindBestPosition(BuildingType)` then scans the whole
map and runs several filters and scoring steps per candidate.

The expensive part is not only the outer map loop. Each candidate may also
trigger:

- borderland checks
- minimal-resource checks
- resource-penalty calculations
- proximity checks against existing buildings
- fishery or quarry validation
- build-quality penalty estimation

### 2. The same resource values are recomputed several times

The global search path still calls `AIQueryService::CalcResourceValue(...)`
from multiple helper functions for the same candidate point and resource.
That multiplies the cost of each search.

### 3. Proximity checks are linear in current buildings

`CountUsualBuildingInRadius(...)`, `OtherUsualBuildingInRadius(...)`, and
`OtherStoreInRadius(...)` scan current buildings and building sites directly.
That is acceptable for a few local searches, but expensive inside a
whole-map global search.

### 4. Connect jobs perform repeated pathfinding

`ConnectFlagToRoadSytem(...)` loops over nearby flags and may do all of the
following repeatedly:

- `FindFreePathForNewRoad(...)`
- `FindPathOnRoads(...)` to the nearest storehouse flag
- `FindPathOnRoads(...)` again to check whether the two flags are already
  connected
- `EstimateRoadRouteBQPenalty(...)`

This makes connect jobs expensive even when they do not end up building a
road.

### 5. Normal build jobs do secondary-road optimization inline

After a building is placed and connected once, non-military jobs immediately
run `BuildAlternativeRoad(...)`. That is another candidate loop with
pathfinding and route scoring inside the main build-job pipeline.

## Proposals

## 1. Add a real cache for global best positions

### Goal

Avoid repeated full-map rescans for the same `BuildingType`.

### Proposal

Keep a cached result per `BuildingType` inside `GlobalPositionFinder`, with a
generation counter for invalidation.

Cache both:

- successful results
- negative results (`MapPoint::Invalid()`)

Invalidate on coarse world-state changes first:

- `UpdateNodesAround(...)`
- `SetFarmedNodes(...)`
- building creation or destruction
- building-site creation or destruction
- ownership / border changes
- harbor availability changes
- BQ refreshes that actually changed nodes

### Why this matters

The profiler shows that global build jobs are the single largest construction
cost. This is the highest-payoff change.

### Notes

There are already existing proposal files for this area:

- `proposals/global-position-finder-performance.md`
- `proposals/global-position-finder-cache-best-positions-plan.md`

This document treats that cache as the first priority, not as an optional
follow-up.

## 2. Memoize resource values during global search

### Goal

Stop recalculating the same resource sums several times per candidate.

### Proposal

For one call to `GlobalPositionFinder::FindBestPosition(bt)`, build a small
per-point scratch cache of resource values that are actually touched during
the search.

A simple version is enough:

- key: `MapPoint + AIResource`
- value: already computed `CalcResourceValue(...)`

Use the cached value in:

- border blocking
- minimal-resource checks
- base resource rating
- resource penalties
- reserved military border-slot checks where possible

### Better version

Where semantics allow it, read from `AIMapState` resource maps instead of
calling `CalcResourceValue(...)` again.

### Why this matters

This reduces the cost of every global candidate even if the cache from
proposal 1 misses.

## 3. Use a two-phase global search

### Goal

Run expensive checks only on a shortlist instead of on every eligible tile.

### Proposal

Split `FindBestPosition(bt)` into:

1. cheap whole-map scan
2. expensive scoring on top `N` candidates

Cheap phase should include:

- `reachable`
- `owned`
- `farmed`
- BQ size compatibility
- simple harbor exclusion
- cached resource thresholds

Expensive phase should include:

- proximity checks
- rating bonuses from nearby buildings
- `EstimateBuildLocationBQPenalty(...)`
- fishery and quarry validation

### Why this matters

The map scan itself is not the whole problem. Many expensive checks happen
late. Shortlisting keeps behavior similar while cutting worst-case cost.

## 4. Replace linear proximity scans with a spatial index

### Goal

Make nearby-building queries cheap.

### Proposal

Maintain a simple AI-owned spatial index for buildings and building sites.
Practical options:

- coarse bucket grid keyed by map sector
- per-building-type occupancy buckets
- a lightweight radius-query helper built from those buckets

Use it for:

- `CountUsualBuildingInRadius(...)`
- `OtherUsualBuildingInRadius(...)`
- `OtherStoreInRadius(...)`
- fishery and forester spacing checks in local placement

### Why this matters

These helpers are currently used both by global search and by local placement.
The same investment improves multiple hotspots.

## 5. Cache road-network connectivity to warehouses

### Goal

Avoid repeated `FindPathOnRoads(...)` checks during connect jobs.

### Proposal

Track connected road components or maintain a lazily refreshed connectivity
map seeded from storehouse flags.

At minimum, make these operations cheap:

- "is this flag connected to any storehouse?"
- "is this candidate flag connected to the same road component?"

That allows `ConnectFlagToRoadSytem(...)` and `BuildAlternativeRoad(...)` to
reject many candidates before any expensive pathfinding.

### Why this matters

`ExecuteConnectJobs` is the second-largest construction hotspot after global
search.

## 6. Make secondary-road optimization asynchronous or selective

### Goal

Keep normal build jobs focused on getting the building online quickly.

### Proposal

Do not run `BuildAlternativeRoad(...)` immediately for every successful
non-military build.

Instead:

- enqueue a low-priority road-improvement job, or
- only run it when the first road is long enough, or
- only run it for high-value building classes

Good selectors:

- route length above a threshold
- mines and core production buildings only
- low road-network density near the site

### Why this matters

This should reduce `ExecuteBuildJobs` without changing whether the building
gets placed and connected at all.

## 7. Prune connect-job candidate flags earlier

### Goal

Reduce the number of pathfinding attempts per connect job.

### Proposal

Before calling `FindFreePathForNewRoad(...)`, filter nearby flags with cheap
heuristics:

- direct distance to the origin flag
- direct distance to the target warehouse flag
- reject flags that obviously move away from the warehouse
- reject flags already marked in recent failed attempts
- reject flags in the same temporary construction exclusion zone

Also consider keeping a short-lived negative cache for:

- "flag A could not connect to flag B"
- "candidate flag is not warehouse-connected"

### Why this matters

The current implementation often discovers that a candidate is bad only after
pathfinding work has already happened.

## 8. Add negative-result caching for expensive validation helpers

### Goal

Avoid repeating path-based validation for the same bad points.

### Proposal

Cache recent failures for checks like:

- `ValidFishInRange(...)`
- quarry stone-access validation
- impossible road-connection targets

These caches can be:

- small
- per-building-type
- generation-based

Invalidate them on nearby map changes rather than on every frame.

### Why this matters

The profile shows frequent retries. If the world did not change in a relevant
way, repeating the same expensive validation is waste.

## 9. Make queue deduplication constant-time

### Goal

Reduce overhead from queue management and repeated retries.

### Proposal

Add side sets for queued work:

- global jobs by `BuildingType`
- local build jobs by `(BuildingType, around, searchMode)`
- connect jobs by flag position

That avoids linear scans in:

- `AddGlobalBuildJob(...)`
- `AddBuildJob(...)`
- `AddConnectFlagJob(...)`

### Why this matters

This is not the biggest hotspot, but it is low-risk and helps once the major
search/pathfinding costs have been reduced.

## Recommended Order

If the goal is best payoff per implementation effort, I would do the work in
this order:

1. cache global best positions per building type
2. memoize resource values inside global search
3. add a two-phase global search
4. add a spatial index for proximity checks
5. cache warehouse-road connectivity
6. move secondary-road optimization out of the critical build path
7. add smaller queue and negative-result caches

## Expected Outcome

If only the first half of the list lands cleanly, the likely effect is:

- much less time in `ExecuteGlobalBuildJobs`
- noticeable reduction in `ExecuteConnectJobs`
- smaller but still useful reduction in `ExecuteBuildJobs`

That should lower both average AI frame cost and worst-case spikes, especially
in late-game states with many buildings and many retries.
