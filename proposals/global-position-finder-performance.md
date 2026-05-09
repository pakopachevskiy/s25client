# Global Position Finder Performance Proposals

This note summarizes the recent headless AI profiling results and proposes
concrete optimizations for `GlobalPositionFinder::FindBestPosition()`.

## Findings

Profiling on the `Dragons.wld` headless AI battle workload showed that the
slowdown is not caused by `AIMilitaryLogistics::UpdateTroopsLimit()`.

The dominant path was:

- `AIEventHandler::ExecuteAIJob()`
- `AIConstruction::ExecuteJobs()`
- global build jobs
- `BuildJob::TryToBuild()`
- `GlobalPositionFinder::FindBestPosition()`

The expensive part is not one single branch inside the function. The problem is
that global build jobs repeatedly trigger a full-map search, and each candidate
point can also perform additional expensive work.

Current cost drivers:

- full-map scan in `GlobalPositionFinder::FindBestPosition()`
- repeated `CalcResourceValue(...)` calls for the same point and resource
- repeated proximity checks that linearly scan buildings and building sites
- per-candidate `EstimateBuildLocationBQPenalty(...)`
- repeated retries of global jobs even when the world state did not change in a
  way that could improve the answer

## Main Proposals

## 1. Cache best positions per building type

### Goal

Avoid repeated full-map scans for the same `BuildingType`.

### Idea

Store the best global position for each `BuildingType` together with a dirty
generation. Recompute only when relevant inputs change.

### Invalidation candidates

- ownership changes
- reachable-node updates
- building-quality changes
- building and building-site creation or removal
- farmed-node changes
- harbor availability changes

### Expected impact

This should be the highest-payoff change because the profile showed frequent
time inside global build jobs, not just one unusually slow search.

## 2. Replace repeated resource queries with cached resource-map reads

### Goal

Stop recomputing resource values for every candidate point.

### Idea

`AIMapState` already owns `AIResourceMap` instances. Where possible,
`GlobalPositionFinder` should use `GetResMapValue(pt, resource)` instead of
calling `queries.CalcResourceValue(pt, resource)` again.

### Good targets

- border blocking
- minimal resource requirements
- base resource rating
- resource penalties that only depend on resource-map values

### Expected impact

Medium to high. This reduces work per candidate without changing behavior.

## 3. Split the search into cheap and expensive phases

### Goal

Run the most expensive checks only on a small shortlist.

### Idea

Use a two-stage search:

1. Scan the whole map with only cheap filters and a coarse score.
2. Keep the best `N` candidates.
3. Run expensive checks only for those `N`.

### Cheap-phase candidates

- `reachable`, `owned`, `farmed`
- building-quality size check
- cached resource thresholds
- cached borderland or harbor exclusions

### Expensive-phase candidates

- proximity checks
- `EstimateBuildLocationBQPenalty(...)`
- quarry-specific and fish-specific validation
- detailed bonus computation

### Expected impact

High if the expensive checks are currently rejecting many points late.

## 4. Throttle retries for unchanged global jobs

### Goal

Prevent the AI from recomputing the same failed global search over and over.

### Idea

If a global build job failed to find a valid point, do not retry it until some
relevant world-state generation changes.

### Expected impact

High in practice if the queue repeatedly re-enqueues global jobs while no new
space or resources became available.

## Secondary Proposals

## 5. Add spatial indexes or heatmaps for proximity queries

### Goal

Avoid scanning building lists for every candidate point.

### Current bottleneck

These helpers are linear in the number of buildings or sites:

- `CountUsualBuildingInRadius(...)`
- `OtherUsualBuildingInRadius(...)`
- `OtherStoreInRadius(...)`

### Idea

Maintain per-building-type influence maps or coarse spatial buckets, so
proximity queries become constant-time or near-constant-time.

### Expected impact

Medium to high, but more invasive than the cache-based changes above.

## 6. Special-case building types with dedicated candidate sets

### Goal

Avoid scanning the whole map when only a small subset of points can ever be
valid.

### Examples

- quarries near granite
- fisheries near fish-accessible coast
- harbors near harbor positions
- mines on mine-quality terrain only

### Idea

Precompute or maintain candidate point lists per building class and search only
those.

### Expected impact

Medium. Strong for special building types, weaker for general-purpose
structures.

## 7. Reduce temporary work inside the loop

### Goal

Trim overhead without changing architecture.

### Examples

- hoist `BuildingProperties::IsMilitary(bt)` out of the loop
- pre-resolve config references used per point
- avoid repeated access through long call chains
- keep point scoring branch order cheap-to-expensive

### Expected impact

Low to medium. Useful after the larger algorithmic changes, not before.

## Recommended Implementation Order

1. Cache best positions per `BuildingType` with explicit invalidation.
2. Use cached resource-map values instead of repeated `CalcResourceValue(...)`.
3. Add retry throttling for global jobs that failed without relevant world
   changes.
4. Convert the search to a two-stage shortlist evaluation.
5. Add spatial indexes for proximity and neighbor-count queries if still needed.

## Suggested Validation

- rerun the same headless save window used for profiling
- compare total wall-clock time and GF/sec
- log how many times `FindBestPosition()` is called per `BuildingType`
- log cache hit rate if a best-position cache is added
- verify that chosen build positions remain stable or intentionally equivalent
