# AI Road Workload Debug Overlay

This note describes the JH AI road-workload diagnostic: what a workload score
means, how the snapshot is calculated, and how the AI Debug window renders it.

## Purpose

The road-workload snapshot estimates which parts of a player's road network
are likely to carry the most ware traffic. It is used both as a diagnostic aid
for inspecting logistics topology and as a low-frequency AI signal for adding
shortcut roads around hot segments. It is not a measurement of actual carrier
throughput, queue length, or delivered ware count.

Each score is attached to an owned flag-to-flag `RoadSegment`. A score is the
number of eligible ware-flow edges whose current ware-mode route crosses that
segment. Every eligible edge contributes exactly `1`, regardless of ware
quantity, production rate, building productivity, or consumer priority value.

Selecting the overlay does not change AI behavior; it only renders the latest
cached values.

## Ware-Flow Model

The calculator models three classes of potential ware flow:

```text
compatible enabled producer -> eligible consumer
enabled producer            -> every completed warehouse
every completed warehouse   -> eligible consumer
```

The first class models direct supply. The warehouse classes treat storage
buildings as stable logistics hubs, so a road remains visible as potentially
important even when the warehouse is temporarily empty.

Headquarters, storehouses, and harbor buildings are warehouses for this model.
Current warehouse stock and inventory settings do not affect hub eligibility.

### Producers

A producer is a completed usual building when:

- `BLD_WORK_DESC[type].producedWare` exists
- the produced ware is not `GoodType::Nothing`
- production is enabled

Disabling production removes that building's producer edges on the next
snapshot refresh.

### Consumers

A completed usual building contributes one consumer edge for each required
ware whose `CalcDistributionPoints(good)` value is greater than `0`. A full
consumer buffer therefore removes that edge on the next refresh. The numeric
distribution priority is used only as an eligibility test; it does not weight
the workload score.

Temporary consumers are included as well:

- construction sites contribute missing boards and stones
- active harbor expeditions contribute missing boards and stones
- military buildings contribute coins when `CalcCoinsPoints()` is greater
  than `0`

Each material type is a separate edge. For example, a construction site
missing both boards and stones contributes two warehouse-to-consumer routes.

## Route Calculation

`AIRoadWorkload::Refresh()` initializes every registered owned flag-to-flag
road segment with score `0`. This ensures that unused roads remain visible in
the overlay.

For each modeled edge, the calculator skips same-endpoint pairs and calls:

```cpp
queries_.FindPathForWareOnRoads(start, goal, nullptr, &route)
```

This forwards to `RoadPathFinder::FindPath(..., wareMode = true, ...)`.
Ware-mode pathfinding follows normal ware-routing behavior:

- carrier congestion penalties influence the selected route
- normal land roads are allowed
- upgraded donkey roads are allowed
- waterways are allowed
- ship connections are allowed

The optional pathfinder output contains traversed `RoadSegment` pointers in
start-to-goal order. Ship transitions are omitted because they do not
correspond to a road segment. Road portions before and after a ship hop remain
in the returned route and receive workload scores.

Disconnected pairs do not contribute. Each segment in a successful routed
edge is incremented once. The calculator only scores registered owned
flag-to-flag roads; building attachment sections are not rendered as workload
roads.

## Snapshot Storage

The calculated segment scores are expanded into a node-local cache:

```text
road segment score -> each non-flag interior road tile
```

Endpoint flags are intentionally skipped. A flag is a junction and may belong
to several segments with different scores, so a single node-local value would
be ambiguous. A one-step road segment has no interior tile and therefore has
no rendered number.

Each cached node contains `std::optional<unsigned>`:

- `std::nullopt` means no workload value should be rendered at the tile
- `0` means the tile belongs to a registered road segment with no modeled
  routed traffic
- a positive value is the number of modeled routed edges crossing the segment

The cache is replaced as a whole during refresh. Debug rendering only reads
the cached values.

The refresh also stores one segment-level record per registered road:

```text
endpoint flag positions, segment score, segment length, water-road marker
```

That segment list feeds the AI's global workload-bypass activation. The tile
cache remains the source for debug overlay rendering.

## Refresh Cadence

`AIPlayerJH` owns one `AIRoadWorkload` instance. It refreshes the snapshot:

1. once after AI startup initialization
2. during the existing player-staggered economic-maintenance cadence:

```cpp
if((gf + playerId * 13) % 1500 == 0)
```

3. during the player-staggered global workload-bypass cadence:

```cpp
if((gf + playerId * 23) % 2500 == 0)
```

The stagger avoids recalculating all AI players on the same game frame.
Between refreshes, overlay values may be stale for up to `1500 GF`. That is
intentional: this is a low-frequency diagnostic snapshot, not live transport
telemetry.

Refresh cost is recorded in the `CalculateRoadWorkload` runtime-profiler
section. Its work-unit value is the number of attempted pair routes after
same-endpoint pairs have been skipped. Disconnected attempts count as work
units even though they do not add a score.

## AI Bypass Use

After the `2500 GF` refresh, `AIPlayerJH` inspects hot land segments with a
workload score of at least `600`, capped to the first `8` sorted hotspots. For
each candidate, `AIConstruction::BuildAlternativeRoadBypassingSegment()` looks
for a new land road whose current road-network path crosses the hot segment.

The build attempt is intentionally conservative:

- candidate endpoint flags are searched near both hot-segment endpoints,
- the proposed free-terrain road must be no more than `24` steps,
- the new road's effective length, including road-route BQ penalty, must be
  lower than the current road path that uses the hot segment,
- an already existing path around the hot segment suppresses the build when it
  is no longer than the proposed new road.

Only one bypass can be queued per activation.

## Overlay Rendering

`Road Workload` is appended to the AI Debug overlay list as overlay id `16`.
Existing overlay ids are unchanged.

`iwAIDebug::DebugPrinter::onDraw(...)` is invoked for visible map nodes by the
normal world-view draw callback. For the road-workload overlay it performs a
cached lookup:

```cpp
const std::optional<unsigned> workload = ai->GetRoadWorkload(pt);
if(workload)
    font.Draw(curPos, helpers::toString(*workload), FontStyle{}, COLOR_YELLOW);
```

The optional check still renders `0`, because `std::optional<unsigned>{0}` has
a value. Non-road tiles, endpoint flags, and road sections without a cached
interior node value draw no text.

Changing the selected overlay only changes the renderer's overlay id. It does
not trigger `AIRoadWorkload::Refresh()`.

## Example

Consider a shared road section used by:

```text
sawmill -> metalworks
sawmill -> headquarters
headquarters -> metalworks
```

If the sawmill is enabled and the metalworks still accepts boards, each route
adds `1` to every owned flag-to-flag segment it crosses. A shared segment
crossed by all three routes displays `3`. If the metalworks board buffer
becomes full, the consumer edge disappears at the next refresh and the shared
segment displays `1` for the remaining producer-to-warehouse route.

## Practical Trace

The important files are:

- `libs/s25main/ai/aijh/runtime/AIRoadWorkload.*`: builds eligible edges,
  calculates routes, stores segment scores, and expands segment values onto
  tiles
- `libs/s25main/pathfinding/RoadPathFinder.*`: optionally returns traversed
  road segments while omitting ship hops
- `libs/s25main/ai/AIQueryService.*`: exposes ware-mode road path queries and
  registered player roads, including path searches that avoid one segment
- `libs/s25main/ai/aijh/planning/AIConstruction.*`: builds global workload
  bypass shortcuts
- `libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp`: owns the refresh and bypass
  cadence
- `libs/s25main/ai/aijh/debug/AIDebugView.h`: exposes cached debug reads
- `libs/s25main/ingameWindows/iwAIDebug.cpp`: appends the overlay and renders
  cached numeric values
- `libs/s25main/ai/aijh/debug/AIRuntimeProfiler.*`: records refresh cost

The shortest rendering chain is:

```text
AIPlayerJH::RunGF(...)
  -> AIRoadWorkload::Refresh()
  -> cached optional score per interior road tile

iwAIDebug::DebugPrinter::onDraw(pt, curPos)
  -> ai->GetRoadWorkload(pt)
  -> AIRoadWorkload::Get(pt)
  -> font.Draw(...) when the cached optional has a value
```
