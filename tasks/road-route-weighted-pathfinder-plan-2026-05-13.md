# AI Road Route Weighted Pathfinder Plan

## Goal

Make AI road route selection consider route quality during free-terrain path
search, not only after one shortest route has already been chosen for each
candidate flag.

This is a planning document only. No code changes are included here.

## Problem

`AIConstruction::ConnectFlagToRoadSytem()` currently compares candidate flags
using a score that includes route BQ damage:

```text
score = oddPenalty + 2 * newRoadLength + warehouseRoadDistance
        + 10 * maxNonFlaggableRun
        + bqPenalty.roadRoute * routeBQPenalty
```

However, each candidate flag gets only one route from
`AIQueryService::FindFreePathForNewRoad()`, which calls
`FreePathFinder::FindPathAlternatingConditions()`.

That pathfinder optimizes only path length plus map-distance heuristic. The BQ
penalty is computed later, after the route is fixed. As a result, the AI picks
between scored flags, but not between multiple plausible routes to the same
flag. A candidate flag can lose because its shortest returned route damages BQ,
even when a slightly longer route to that same flag would have a better final
score.

## Design Principle

Do not move the exact full-route BQ estimator directly into
`FreePathFinder::FindPathAlternatingConditions()`.

The exact estimator in `AIQueryService::EstimateRoadRouteBQPenalty()` is
route-global:

- it builds the full hypothetical road set,
- it tracks affected points,
- it adds hypothetical future flags,
- it recalculates BQ from the route as a whole.

That does not fit the current pathfinder state, which stores only one
predecessor and one length per map point/parity state. Embedding the exact
logic would couple generic pathfinding to AI scoring and would require carrying
large route-dependent state through the open list.

Instead, use a cheap incremental route-cost approximation during search, then
keep the exact existing penalty for final scoring.

## Proposed Implementation

### 1. Add a Weighted Alternating Search API

Add a new overload or sibling method to `FreePathFinder` for weighted
alternating path search.

Suggested API shape:

```cpp
using FP_Node_Cost_Callback = double (*)(const GameWorldBase&, MapPoint from, MapPoint to,
                                        Direction dir, unsigned nextLength, bool nextStepEven,
                                        const void* param);

bool FindPathAlternatingConditionsWeighted(
    MapPoint start,
    MapPoint dest,
    bool randomRoute,
    unsigned maxLength,
    std::vector<Direction>* route,
    unsigned* length,
    Direction* firstDir,
    FP_Node_OK_Callback IsNodeOK,
    FP_Node_OK_Callback IsNodeOKAlternate,
    FP_Node_OK_Callback IsNodeToDestOk,
    FP_Node_Cost_Callback GetNodeCost,
    const void* param);
```

Keep the existing unweighted method for current callers and compatibility.

### 2. Track Weighted Cost Separately From Physical Length

Extend the internal alternating pathfinder state so each parity state stores:

- physical route length, still used for `maxLength` and returned `length`,
- weighted path cost, used for open-list ordering and route replacement,
- predecessor and direction, as today.

Do not replace `length` with weighted cost. Road construction still needs the
real number of route steps.

Possible `NewNode` additions:

```cpp
double cost = 0.0;
double costEven = 0.0;
```

The open-list estimate should become:

```text
estimatedCost = weightedCostSoFar + mapDistanceToDest
```

This keeps the existing admissible-ish distance guidance while allowing route
quality to influence expansion order.

### 3. Allow Better Costs To Replace Visited Node States

The current alternating method marks a node/parity visited once and skips all
later arrivals. That is only valid when every step has the same cost.

For weighted search:

- if a node/parity has not been seen, insert it,
- if it has been seen with higher weighted cost, update predecessor, length,
  direction, and weighted cost,
- put the improved state back into the open list.

The existing implementation uses `std::list` plus `std::min_element`, so the
first implementation can push duplicate open-list entries and ignore stale
entries when popped. That avoids adding iterator bookkeeping immediately.

Stale-pop check:

```text
if popped cost != current stored cost for that node/parity, skip it
```

Use a small epsilon for `double` comparisons.

### 4. Keep Alternating Road Constraints Unchanged

Preserve the current constraints:

- ordinary road-buildable node check,
- alternating flaggable-node check,
- even-step too-close checks against previous even-step locations,
- destination handling,
- `maxLength` pruning.

The weighted pathfinder should change route preference, not route validity.

### 5. Add AI-Specific Approximate Route Cost Callback

Implement the route-cost approximation in `AIQueryService`, not in
`FreePathFinder`.

Suggested public API:

```cpp
bool FindFreePathForNewRoad(
    MapPoint start,
    MapPoint target,
    const BQPenaltyConfig* bqPenalty,
    std::vector<Direction>* route = nullptr,
    unsigned* length = nullptr) const;
```

Or keep the current method and add a clearly named sibling:

```cpp
bool FindWeightedFreePathForNewRoad(MapPoint start, MapPoint target,
                                    const BQPenaltyConfig& bqPenalty,
                                    std::vector<Direction>* route = nullptr,
                                    unsigned* length = nullptr) const;
```

The callback should return:

```text
1.0 + approximateBQCost
```

Where `1.0` is the normal step cost.

Recommended first approximation:

- consider the route point being entered,
- include the same road BQ footprint used by route penalty collection:
  - `pt`
  - `East(pt)`
  - `SouthEast(pt)`
  - `SouthWest(pt)`
- compare current `gwb.GetBQ(affectedPt, playerId)` against a cheap
  hypothetical "road exists at this one point" BQ estimate,
- convert positive BQ drops through `bqPenalty.roadRouteQualityValues`,
- multiply by `bqPenalty.roadRoute`.

This is intentionally local. It will not exactly model cumulative road effects
or future flags; final scoring still handles that.

### 6. Use Weighted Search In AI Road Construction

Update these AI call sites to request weighted route search when road-route BQ
penalties are enabled:

- `AIConstruction::ConnectFlagToRoadSytem()`
- `AIConstruction::BuildAlternativeRoad()`

Keep fallback behavior:

- if `bqPenalty.roadRoute <= 0.0`, use the existing unweighted method,
- optionally, if weighted search fails but unweighted search succeeds, use the
  unweighted route. This preserves current robustness while the weighted search
  is introduced.

### 7. Keep Exact Final Scoring

Do not remove the existing exact final score calculation.

After a route is returned, still compute:

```cpp
aii.Queries().EstimateRoadRouteBQPenalty(start, route, bqPenaltyConfig)
```

and keep the final score formula in `ConnectFlagToRoadSytem()` and
`BuildAlternativeRoad()`.

The weighted search is only a route-generation heuristic. The exact route
penalty remains the authoritative score.

## Testing Plan

### Unit or Integration Coverage

Add a focused test where two routes to the same target are valid:

- a shorter route crosses high-value building ground,
- a slightly longer route avoids it,
- unweighted search would choose the shorter route,
- weighted AI road search chooses the longer lower-penalty route.

Good test locations:

- `tests/s25Main/integration/testBuilding.cpp` if reusing BQ fixtures,
- or a new pathfinding/AI route test near existing pathfinding integration
  coverage.

### Regression Checks

Run at least:

```bash
cmake --build build --target s25client
ctest --test-dir build --output-on-failure
```

If build time is high, first run the targeted test binary/test case, then full
`ctest` before finalizing.

## Risks And Mitigations

### Risk: Weighted Search Becomes Too Expensive

Mitigation:

- keep the callback local and cheap,
- avoid reconstructing full partial routes per neighbor,
- preserve `maxLength`,
- keep the unweighted fallback.

### Risk: Approximation Disagrees With Exact Final Penalty

Mitigation:

- treat weighted search as a bias only,
- keep exact final scoring unchanged,
- tune the local cost multiplier after tests/playtesting.

### Risk: Replacing Visited States Changes Existing Route Determinism

Mitigation:

- add the weighted search as a separate method first,
- leave existing unweighted callers untouched,
- preserve deterministic tie-breaking where possible.

### Risk: BQ Logic Leaks Into Generic Pathfinder

Mitigation:

- keep `FreePathFinder` generic via callbacks,
- implement AI-specific BQ cost calculation in `AIQueryService`.

## Suggested Order Of Work

1. Add the weighted pathfinder state and API without changing callers.
2. Add tests for weighted search using a simple synthetic cost callback.
3. Add the AI road local BQ cost callback in `AIQueryService`.
4. Switch AI road construction to weighted search when `bqPenalty.roadRoute` is
   positive.
5. Add an integration test proving lower-penalty route selection.
6. Run targeted tests and then full `ctest`.

