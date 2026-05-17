# AI Road Route Selection Performance Plan

## Goal

Reduce the runtime cost of AI road route selection after introducing weighted
road routing, while preserving the intended behavior:

- road routes should still avoid expensive building-quality damage where useful,
- final candidate scoring should still use the exact route BQ estimator,
- the unweighted pathfinder should remain available as a robust fallback.

This is a planning document only. No code changes are included here.

## Current Measurement

The performance report in `tmp/path_finder/ai_performance.csv` shows that road
pathfinding dominates AI runtime in the measured game:

```text
Total profiled RunGF time:                  262.1s
Weighted road path search:                 211.1s
Fallback unweighted road path search:       20.4s
Combined road pathfinding cost:            231.5s
Pathfinding share of AI RunGF time:         88.3%
```

Per-call measurements:

```text
FindWeightedFreePathForNewRoad calls:       92,280
FindWeightedFreePathForNewRoad avg:          2.29 ms/call
FindFreePathForNewRoad calls:               59,932
FindFreePathForNewRoad avg:                  0.34 ms/call
Weighted fallback rate:                       65%
```

The weighted search is about `6.7x` slower per call than the old free-road
search, and many candidates pay both costs because weighted search fails and
then falls back to the unweighted pathfinder.

## Main Hypothesis

The regression is multiplicative:

- weighted search is enabled by default through `bqPenalty.roadRoute`,
- it runs for many candidate flags per road connection attempt,
- candidates are currently terrain-path-searched before all cheap rejection
  checks have run,
- weighted search reopens node states and uses an expensive open-list strategy,
- the local BQ cost callback is expensive and called for every expanded edge,
- a high weighted failure rate causes fallback searches for the same candidate.

## Relevant Code

- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`
  - `AIConstruction::ConnectFlagToRoadSytem()`
  - `AIConstruction::BuildAlternativeRoad()`
- `libs/s25main/ai/AIQueryService.cpp`
  - `AIQueryService::FindFreePathForNewRoad()`
  - `AIQueryService::FindWeightedFreePathForNewRoad()`
  - `GetApproximateRoadRouteNodeCost()`
  - `AIQueryService::EstimateRoadRouteBQPenalty()`
- `libs/s25main/pathfinding/FreePathFinder.cpp`
  - `FreePathFinder::FindPathAlternatingConditions()`
  - `FreePathFinder::FindPathAlternatingConditionsWeighted()`
- `libs/s25main/pathfinding/NewNode.h`

## Phase 1: Reduce Unnecessary Path Searches

### 1. Reorder Candidate Rejection In Main Road Connection

In `AIConstruction::ConnectFlagToRoadSytem()`, avoid free-terrain pathfinding
for candidate flags that cannot be selected anyway.

Current expensive order:

1. find free-terrain route to candidate,
2. inspect route,
3. check candidate road path to warehouse,
4. check whether candidate is already connected to the source flag.

Proposed order:

1. skip candidate if it is already connected to the source flag,
2. compute or reject candidate road path to target warehouse,
3. run free-terrain road pathfinding only for surviving candidates,
4. inspect and score the returned route.

Expected impact:

- reduces weighted pathfinder calls directly,
- especially helps dense road systems where many nearby flags are already
  connected or do not lead to the target warehouse.

Validation:

- compare `FindWeightedFreePathForNewRoad_Calls` before and after,
- compare final route-building behavior in integration tests and AI smoke runs,
- ensure `warehouseRoadDistance` remains available for scoring.

### 2. Reorder Candidate Rejection In Secondary Roads

In `AIConstruction::BuildAlternativeRoad()`, keep the existing cheap
`IsConnectedToRoadSystem()` check before terrain pathfinding, then look for
more cheap rejections that can safely move earlier.

Potential candidates:

- skip the main-route endpoint before pathfinding,
- compute existing road distance before new free-terrain pathfinding when it
  gives a usable upper bound,
- use straight map distance to reject candidates whose minimum possible
  `newLength * lengthFactor` is already worse than `oldLength`.

Expected impact:

- fewer weighted searches during secondary-road attempts.

Validation:

- compare secondary-road construction count and pathfinder call count,
- inspect whether shortcut roads still appear in normal AI games.

## Phase 2: Use Weighted Search As Refinement

### 3. Add Old-First Candidate Evaluation

Use the unweighted pathfinder as the broad first pass. Weighted search should
not run for every scanned flag.

Proposed main-road flow:

1. For each candidate that passes cheap road-network filters, run
   `FindFreePathForNewRoad()`.
2. Score the unweighted route using the existing exact final scoring formula,
   including `EstimateRoadRouteBQPenalty()`.
3. Keep only the best few candidates for weighted refinement.
4. Run `FindWeightedFreePathForNewRoad()` for those candidates.
5. Re-score weighted routes with the exact estimator and select the best final
   route.

Suggested first configuration:

```text
weightedRefinementTopN = 3
weightedRefinementScoreMargin = 25.0
```

Run weighted refinement for candidates that are either in the top `N` or within
the score margin of the current best unweighted candidate.

Expected impact:

- turns `O(all candidate flags)` weighted calls into `O(best candidate flags)`,
- preserves BQ-aware route generation where it can change the final decision.

Validation:

- compare weighted call count and total weighted time,
- compare route BQ penalty distribution before and after,
- confirm the selected route score is not systematically worse than the current
  all-weighted behavior.

### 4. Apply The Same Refinement Strategy To Secondary Roads

For `BuildAlternativeRoad()`, first determine whether an unweighted route is
promising enough to beat the existing road distance. Only run weighted search
when the unweighted candidate is near the acceptance threshold.

Suggested rule:

```text
unweightedEffectiveLength = newLength * lengthFactor
run weighted only if unweightedEffectiveLength < oldLength + margin
```

The exact final BQ penalty still decides whether the route is built.

Validation:

- compare secondary-road success rate,
- compare pathfinder calls per 1000 GF,
- inspect games for excessive loss of useful shortcut roads.

## Phase 3: Add Lower-Bound Pruning

### 5. Prune Candidates That Cannot Beat The Current Best

Once there is a current best score, skip candidate flags whose theoretical
minimum score cannot win.

For main roads:

```text
minScore = 2 * mapDistance(sourceFlag, candidateFlag)
           + warehouseRoadDistance
```

Ignore BQ penalty and non-flaggable penalty in the lower bound because they can
only increase the real score.

For secondary roads:

```text
minEffectiveNewLength = mapDistance(sourceFlag, candidateFlag) * lengthFactor
```

If the minimum cannot satisfy the build condition, skip terrain pathfinding.

Expected impact:

- reduces pathfinder calls after a good candidate has already been found,
- especially useful because `FindFlags()` tends to return nearby flags first.

Validation:

- add debug counters or profiler work units for pruned candidates,
- compare selected route scores before and after on deterministic test games.

## Phase 4: Fix Weighted Search Failure Rate

### 6. Investigate Weighted Failure Cases

The current measured fallback rate is about `65%`. Weighted search is supposed
to change route preference, not route validity, so this is a key problem.

Add temporary diagnostic counters in `FindWeightedFreePathForNewRoad()`:

- weighted success,
- weighted failure followed by unweighted success,
- weighted failure followed by unweighted failure,
- average map distance for each bucket,
- average unweighted returned length for weighted-failure/unweighted-success.

Expected finding:

- weighted search may replace a node/parity state with a lower weighted cost
  but longer physical route, then later hit the `maxLength = 100` physical
  length limit.

Validation:

- reproduce the high fallback rate in `ai_performance.csv`,
- capture at least a few representative start/target pairs for debugger or
  focused test coverage.

### 7. Preserve Physical-Length Viability During Weighted Replacement

Review the state replacement logic in
`FreePathFinder::FindPathAlternatingConditionsWeighted()`.

Current replacement uses weighted cost as the main state quality. That is not
always sufficient because path validity also depends on physical length.

Candidate fixes:

- reject replacement if the new state has lower weighted cost but much longer
  physical length and the old state remains viable,
- use a lexicographic comparison with weighted cost first and physical length as
  a tie-break within a configurable epsilon,
- keep two labels per node/parity when there is a meaningful cost/length tradeoff
  instead of only one stored state.

Start with the simplest safe change:

```text
replace only when:
  newWeightedCost + epsilon < oldWeightedCost
  or abs(newWeightedCost - oldWeightedCost) <= epsilon and newLength < oldLength
```

If fallback rate remains high, consider multi-label state storage.

Validation:

- fallback rate should drop significantly,
- weighted pathfinder should still return BQ-aware routes in existing tests,
- route length must still obey `maxLength`.

## Phase 5: Optimize Weighted Search Internals

### 8. Replace Linear Open-List Scans

`FindPathAlternatingConditionsWeighted()` currently uses `std::list` plus
`std::min_element`. With reopened states, this creates many stale duplicate
entries and each pop scans the whole open list.

Replace it with a heap:

- use `std::priority_queue` with a reversed comparator, or
- reuse an existing project heap helper if it fits the duplicate/stale-entry
  model.

Keep stale-entry skipping:

```text
if popped cost does not match stored node/parity cost, skip it
```

Expected impact:

- lower per-call weighted search cost,
- most visible in difficult route searches with many reopened states.

Validation:

- compare `FindWeightedFreePathForNewRoad_AvgUsPerCall`,
- run pathfinding integration tests,
- verify deterministic route output if tests depend on tie-breaking.

### 9. Cache Approximate Node Costs Per Search

`GetApproximateRoadRouteNodeCost()` recalculates a local BQ approximation for
every edge expansion. The result mostly depends on the entered map point and
the player/config.

Add a per-search cache in the road pathfinding parameter:

```cpp
mutable std::vector<double> approximateCostByNode;
mutable std::vector<unsigned> approximateCostVisit;
mutable unsigned approximateCostVisitId;
```

Or use a smaller local cache keyed by map index if modifying the parameter is
too intrusive.

Cache the cost for `to`:

```text
stepCost(to) = 1.0 + bqPenalty.roadRoute * approximateLocalBQPenalty(to)
```

Expected impact:

- reduces repeated `BQCalculator` and hypothetical BQ work,
- especially useful when a node is approached from several directions.

Validation:

- compare weighted average microseconds per call,
- verify no route score changes caused by caching,
- ensure cache lifetime is one pathfinder invocation or otherwise invalidated
  correctly.

## Phase 6: Make Behavior Tunable

### 10. Add AI Config Knobs For Rollout

Add conservative config options so the weighted feature can be tuned without
code changes:

```yaml
bqPenalty:
  roadRoute: 1.0
  roadRouteWeightedSearch: true
  roadRouteWeightedRefinementTopN: 3
  roadRouteWeightedRefinementScoreMargin: 25.0
```

Alternatively, place the refinement controls under a dedicated route-selection
section if the config structure already has a better home.

Defaults should preserve BQ-aware route behavior but avoid all-candidate
weighted search.

Validation:

- config parser tests for new fields,
- default config test expectations,
- manual override with weighted search disabled.

## Recommended Implementation Order

1. Reorder cheap candidate rejection before terrain pathfinding.
2. Add old-first weighted refinement for main roads.
3. Add old-first weighted refinement for secondary roads.
4. Add lower-bound pruning.
5. Diagnose and reduce weighted failure rate.
6. Replace weighted open list with a heap.
7. Cache approximate node costs.
8. Add or finalize config knobs.

This order prioritizes reducing call count before optimizing individual calls.
The CSV indicates that call count and fallback rate are large enough that this
should produce the fastest meaningful improvement.

## Measurement Plan

Use the existing `AIPerfReporter` columns:

- `RunGF_AvgUsPerGF`
- `FindFreePathForNewRoad_AvgUsPerGF`
- `FindFreePathForNewRoad_Calls`
- `FindWeightedFreePathForNewRoad_AvgUsPerGF`
- `FindWeightedFreePathForNewRoad_Calls`
- `FindWeightedFreePathForNewRoadFallback_AvgUsPerGF`
- `FindWeightedFreePathForNewRoadFallback_Calls`

For each implementation phase, run the same deterministic AI scenario and
record:

```text
weightedTotalUs = sum(FindWeightedFreePathForNewRoad_AvgUsPerGF * WindowGameFrames)
fallbackTotalUs = sum(FindWeightedFreePathForNewRoadFallback_AvgUsPerGF * WindowGameFrames)
runTotalUs      = sum(RunGF_AvgUsPerGF * WindowGameFrames)
pathShare       = (weightedTotalUs + fallbackTotalUs) / runTotalUs
fallbackRate    = fallbackCalls / weightedCalls
```

Success targets for the first optimization pass:

- reduce combined road pathfinding share from `88%` to below `50%`,
- reduce weighted calls by at least `50%`,
- reduce fallback rate materially from `65%`,
- keep route-building behavior stable in integration tests.

## Test Plan

Run focused tests first:

```sh
cmake --build build --target s25Main
ctest --test-dir build --output-on-failure -R Pathfinding
ctest --test-dir build --output-on-failure -R Building
ctest --test-dir build --output-on-failure -R AIConfig
```

Then run a deterministic AI performance scenario with `RTTR_AI_PROFILE=1` and
compare the resulting `ai_performance.csv` against
`tmp/path_finder/ai_performance.csv`.

## Risks

- Reordering filters can subtly change route choice if existing code depended
  on side effects or route computation order.
- Old-first weighted refinement can miss a candidate whose unweighted route
  scores poorly but weighted route would have become best.
- Lower-bound pruning must use only true lower bounds; adding any term that can
  decrease later would make pruning unsafe.
- Heap replacement must preserve deterministic tie-breaking where tests or
  replays rely on route order.
- Node-cost caching must not outlive the world/config state used to compute it.

## Open Questions

- Should weighted refinement be controlled by AI config immediately, or first
  hard-coded for performance experiments?
- What deterministic replay or AI battle setup should become the standard
  benchmark for this route-selection work?
- Is a small loss in BQ-aware route optimality acceptable if road pathfinding
  runtime drops by an order of magnitude?
