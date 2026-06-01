# AI Road Route Selection

See also:

- [construction-mechanics.md](construction-mechanics.md) — `BuildJob`
  flow, `constructionlocations` reservation, and the `Road and Flag
  Utilities` overview that calls into the routines below.
- [configuration.md](configuration.md) — `bqPenalty.roadRoute` knob
  weighting the BQ-degradation term in the scoring formula.
- [position-finding.md](position-finding.md) — picks the building
  position whose flag is the source of these connection searches.

The AI does not run a full global road-network optimizer. Most road
construction is a local heuristic layered on top of two lower-level
pathfinders, with one bounded global pass that looks for high-workload road
segments and tries to add a relieving shortcut.

- `AIConstruction::ConnectFlagToRoadSytem()` chooses which nearby existing flag
  to connect to.
- `AIQueryService::FindFreePathForNewRoad()` finds a road-buildable free-terrain
  route for the new segment.
- `AIQueryService::FindPathOnRoads()` measures the existing road-network
  distance from a candidate flag back to the chosen warehouse.

This document describes how those pieces combine when the AI decides what it
considers the "best" road.

## Trigger Flow

- New building jobs check whether the building flag is already connected to the
  road system.
- If not, `BuildJob::ExecuteJob()` calls
  `AIConstruction::ConnectFlagToRoadSytem()`.
- For non-military buildings, `BuildJob::TryToBuildSecondaryRoad()` may later
  call `BuildAlternativeRoad()` to add a shortcut.
- Independently of building jobs, `AIPlayerJH::RunGF()` periodically refreshes
  the road-workload snapshot and may call
  `BuildAlternativeRoadBypassingSegment()` for a globally hot segment.

## What the AI Optimizes For

The main connection heuristic is not trying to optimize travel time over the
entire transport graph. It is trying to find a nearby owned flag such that:

- the newly built road segment is short,
- the chosen endpoint already reaches a warehouse by road,
- the proposed route leaves room for flags,
- the route avoids long stretches of non-flaggable nodes.

The warehouse target is chosen by straight map distance in
`FindTargetStoreHouseFlag()`, not by current road distance and not by carrier
traffic.

## Candidate Flag Search

`AIConstruction::FindFlags()` gathers candidate endpoints around the source
flag:

- only the AI player's own flags are eligible,
- the search is bounded by a radius (`14` by default for the main connection,
  `10` for alternative roads),
- the result list is capped at `30` flags via `GetPointsInRadius<30>()`.

`GetPointsInRadius()` walks outward ring by ring around the source point in a
fixed order. As a result:

- closer flags are typically seen first,
- dense areas may hit the `30`-flag cap before all plausible endpoints are
  examined,
- the AI therefore picks the best route among the scanned candidates, not among
  every possible flag on the map.

## Building the New Road Segment

For each surviving candidate flag, `ConnectFlagToRoadSytem()` first asks for an
unweighted free-terrain route from the source flag to the candidate via
`AIQueryService::FindFreePathForNewRoad()`. That broad pass keeps the robust old
route finder as the baseline for candidate scoring.

When `bqPenalty.roadRoute` and `bqPenalty.roadRouteWeightedSearch` are both
enabled, the best few unweighted candidates are refined with
`AIQueryService::FindWeightedFreePathForNewRoad(..., allowFallback = false)`.
That query calls `FreePathFinder::FindPathAlternatingConditionsWeighted()`, an
A*-style search with a maximum physical length of `100`.

Weighted refinement is limited by:

- `bqPenalty.roadRouteWeightedRefinementTopN`, default `3`,
- `bqPenalty.roadRouteWeightedRefinementScoreMargin`, default `25.0`.

Candidates are refined when they are in the top `N` unweighted scores or within
the score margin of the best unweighted candidate. If weighted refinement fails,
the already scored unweighted route remains available. Direct callers can still
allow the weighted query to fall back to `FindFreePathForNewRoad()`.

The pathfinder enforces several road-specific constraints:

- every traversed point must be inside player territory,
- every traversed point must be road-buildable according to
  `GameWorldBase::IsRoadAvailable()`,
- on alternating steps, the node must also be flaggable for land roads,
- even-step nodes that are too close to previously chosen even-step nodes are
  rejected.

This alternating-node rule is what makes the produced route compatible with
regular flag placement along the road.

`GameWorldBase::IsRoadAvailable()` rejects nodes that:

- contain blocking objects,
- lie on border stones,
- already carry roads,
- touch prohibited surroundings such as charburner piles,
- have unsuitable terrain.

For land roads, at least one surrounding terrain slot must support something
better than `TerrainBQ::Nothing`, otherwise the point is not considered a valid
road tile.

The weighted search keeps physical route length separate from weighted cost:

- physical length is still used for the `100`-step limit and returned to road
  construction,
- weighted cost is used to order and replace pathfinder states,
- each map point has separate odd/even parity state, as in the alternating
  search.

The weighted implementation uses a heap-backed open list and skips stale
entries when a better state for the same point/parity has already been stored.
Local approximate BQ costs are cached per search by entered map point.

The weighted step cost is AI-specific and intentionally approximate:

```text
stepCost = 1.0 + bqPenalty.roadRoute * approximateLocalBQPenalty
```

The local estimate considers the route point being entered and the same nearby
road-BQ footprint used by exact route scoring:

- `pt`,
- `East(pt)`,
- `SouthEast(pt)`,
- `SouthWest(pt)`.

For those points it compares current BQ against a cheap hypothetical BQ where
the entered route point is treated as road, then converts positive downgrades
through `bqPenalty.roadRouteQualityValues`.

This approximation only guides route generation. It does not replace the exact
route-global BQ estimator used by final scoring.

## Main Connection Scoring

Before terrain pathfinding, `ConnectFlagToRoadSytem()` rejects candidate flags
that are already road-connected to the source flag or cannot reach the chosen
warehouse. Once a best score exists, it also skips candidates whose lower bound
cannot win:

```text
minScore = 2 * mapDistance(sourceFlag, candidateFlag)
           + warehouseRoadDistance
```

After a free-terrain route is found, it filters and scores it.

First it rejects candidates that:

- produce more than `2` consecutive non-flaggable points,
- do not already have a road path to the chosen warehouse flag,
- are already connected back to the source flag through the current road
  network.

Then it computes:

```text
score = oddPenalty + 2 * newRoadLength + warehouseRoadDistance
        + 10 * maxNonFlaggableRun
        + bqPenalty.roadRoute * routeBQPenalty
```

Where:

- `oddPenalty` is `5` when the new road length is odd, otherwise `0`,
- `newRoadLength` is the number of steps in the newly built segment,
- `warehouseRoadDistance` is the existing road-network distance from the
  candidate flag to the selected warehouse,
- `maxNonFlaggableRun` is the longest consecutive stretch of
  `BuildingQuality::Nothing` along the new route,
- `routeBQPenalty` is the summed downgrade cost caused by the hypothetical road
  reducing building quality on route tiles and their nearby affected plots. For
  each affected plot this is `beforeQualityValue - afterQualityValue` when the
  configured value decreases, otherwise `0`. The estimate also includes
  interior flags that the AI will try to place along the new road after
  construction, because those flags can further reduce nearby plot quality,
- `bqPenalty.roadRoute` comes from `AIConfig` and defaults to `1.0`.

The quality values used by `routeBQPenalty` come from
`bqPenalty.roadRouteQualityValues`. Defaults make a `Flag -> Nothing`
downgrade cost `0.5`, while a `Castle -> Hut` downgrade costs `5.0`.

The doubled weight on `newRoadLength` means the AI prefers short new
construction even when the total end-to-end route would be similar either way,
while the BQ term lightly discourages routes that destroy more valuable
building plots. Because route generation now also has a local BQ-aware weight,
the AI can pick a slightly longer free-terrain route to the same candidate flag
when that route avoids expensive building-quality damage.

## How Existing Road Distance Is Measured

`warehouseRoadDistance` comes from `AIQueryService::FindPathOnRoads()`, which
calls `RoadPathFinder::FindPath(..., wareMode = false)`.

This matters because `wareMode = false` means:

- road cost is just the sum of `RoadSegment::GetLength()`,
- water roads are excluded,
- congestion penalties are ignored,
- carrier availability penalties are ignored.

So for road-building decisions the AI measures plain land-road length, not
estimated delivery time.

The road pathfinder itself is another A* search over `noRoadNode` graph nodes
(flags and certain buildings), using:

- `cost = currentCost + roadSegmentLength`,
- `estimate = cost + mapDistanceToGoal`.

It therefore prefers the shortest existing land-road path between two road
nodes.

## Secondary Roads

After a building is connected, non-military jobs may attempt a second road via
`BuildAlternativeRoad()`. Ordinary non-military buildings use the shortcut-only
policy described below. Storehouses use a special policy: the AI tries to build
the first valid secondary road to a connected nearby flag even when the new
segment is longer than the current road-network path.

This pass:

- searches nearby owned flags again,
- requires the candidate to already be connected to the road system,
- computes existing road distance before terrain pathfinding so candidates
  whose straight-distance lower bound cannot win can be skipped,
- computes an unweighted new free-terrain road to that flag,
- runs weighted refinement only when the unweighted route is close enough to
  the build threshold, or when no current road path exists. Storehouses refine
  every valid candidate when weighted refinement is enabled,
- compares that new segment against the current road-network distance.

The road is built only if:

- no current path exists, or
- `newLength * 5 + bqPenalty.roadRoute * routeBQPenalty < oldLength`.

So secondary roads for ordinary buildings are conservative shortcuts. They are
not meant to fine-tune every route, only to add clearly superior bypasses.
Storehouse secondary roads are intentionally less conservative and may create
longer local loops as long as the route passes the same ownership,
connectivity, flag-placement, and road-buildability checks.

## Global Workload Bypasses

The AI also has a low-frequency global activation for road segments that the
workload model considers hot. Every `2500 GF`, staggered by player id, it
refreshes `AIRoadWorkload` and inspects the highest-scoring land road segments
whose workload is at least `600`.

For each inspected hot segment, the bypass search:

- looks at owned flags near each endpoint, including the endpoint flags
  themselves,
- caps each endpoint side to the nearest `12` candidate flags within radius
  `10`,
- requires the current land-road path between the candidate pair to cross the
  hot segment,
- skips pairs whose straight map distance is already no better than the
  current road distance,
- finds a new free-terrain land road of at most `24` steps,
- rejects routes with more than `2` consecutive non-flaggable points,
- applies the same road-route BQ penalty and optional weighted refinement used
  by other road-building code,
- skips the pair if an existing path that avoids the hot segment is already no
  longer than the proposed new road.

Only one best bypass is built per activation. Unlike the building-local
secondary road pass, this trigger is not tied to a newly completed building;
the hot workload segment chooses the area of interest.

## Waterway Shortcuts

After the ordinary secondary-road attempt, suitable non-military building jobs
may also try `BuildAlternativeWaterRoad()`. Waterways are ware-logistics
shortcuts, not building or worker connections.

The waterway pass:

- searches owned flags within radius `10`,
- requires both endpoints to remain connected to the land-road system,
- finds the new route with `FindFreePathForNewWaterRoad()`, using water terrain
  checks without land-road alternating flag-placement constraints,
- compares the candidate against the existing ware-capable road-network route,
- builds only when `newWaterwayLength * 5 < oldWareRouteLength`,
- requires a reachable warehouse that can provide a helper and boat.

Completed waterways do not receive AI-added interior flags. The economy keeps a
reserve of two stored boats while waterways exist or are planned. When the
reserve is low, one shipyard is temporarily switched to small-boat production
and enabled. Once the reserve is restored, that shipyard returns to the
existing large-ship policy.

## Important Non-Obvious Details

- `FindTargetStoreHouseFlag()` uses nearest warehouse by straight map distance,
  not by road distance.
- `FindFlags()` is capped to `30` results, so dense local flag clusters can hide
  farther-but-better options.
- Weighted route search changes route preference, not route validity. The same
  road-buildable, alternating flaggable-node, and spacing constraints still
  apply.
- Weighted search is a refinement step, not the broad candidate search. The
  unweighted route remains the fallback route for candidates that are not
  refined or whose weighted refinement fails.
- The weighted search uses a local BQ approximation. The final route score still
  uses `EstimateRoadRouteBQPenalty()`, which models the full hypothetical road
  and future interior flags.
- `MinorRoadImprovements()` currently returns immediately into `BuildRoad()`.
  The code below that early return is effectively disabled.
- The construction heuristics are greedy. The workload bypass activation scans
  globally for hot segments, but each build attempt still uses a bounded local
  candidate search around that segment's endpoints.

## Practical Takeaways

- The AI usually prefers connecting to a nearby flag that already lies on a
  short route to the nearest warehouse.
- It avoids routes that create long non-flaggable stretches, because those are
  fragile for flag placement and logistics.
- Main building-connection routing does not account for live carrier
  congestion when selecting the endpoint for a new road.
- If a route choice looks odd in-game, likely causes are:
  - a better flag was outside the search radius,
  - a better flag was beyond the `30`-candidate cap,
  - the nearest warehouse by map distance was not the best warehouse by road,
  - the free-terrain road pathfinder rejected the more obvious route due to
    flag-placement constraints,
  - the weighted route search avoided a shorter route because its local BQ
    damage estimate was higher.
