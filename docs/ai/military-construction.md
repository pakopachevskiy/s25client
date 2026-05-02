# AI Military Construction

This note describes how the JH AI selects military building locations and how
it limits simultaneous military construction.

See also:

- `docs/ai/construction-mechanics.md` - general build-job queues, reservation
  tracking, and `Wanted()`.
- `docs/ai/position-finding.md` - full-map position scoring for global jobs.
- `libs/s25main/ai/aijh/runtime/AIEconomyController.cpp`
- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`
- `libs/s25main/ai/aijh/planning/Jobs.cpp`
- `libs/s25main/ai/aijh/runtime/AIWorldQueries.cpp`
- `libs/s25main/ai/aijh/runtime/AIResourceMap.cpp`

## Entry Points

Regular military expansion starts in
`AIEconomyController::PlanNewBuildings()`.

On each build-planning interval the AI may enqueue military build jobs around:

- a random warehouse, once the game frame is above `1500` or the AI has more
  than `11` boards
- a random existing military building, if any exist

Both paths call `AIEconomyController::AddMilitaryBuildJob(MapPoint pt)`, which
uses the point as an anchor for type selection only. It first asks
`AIConstruction::ChooseMilitaryBuilding(pt)` which military building type to
try, then routes the job based on the chosen type:

- if `BuildingProperties::IsMilitary(*milBld)` (Barracks, Guardhouse, Watchtower,
  Fortress): calls `AddGlobalBuildJob(*milBld)` so the final tile is chosen by
  `GlobalPositionFinder::FindBestPosition(type)`.
- otherwise (Catapult): calls `AddBuildJob(*milBld, pt, false, true)` keeping
  the original local radius search.

The final construction tile is not chosen when the job is enqueued. It is
chosen later when `BuildJob::TryToBuild()` executes the job.

`Catapult` remains on the local radius path because
`BuildingProperties::IsMilitary(Catapult)` is false, and `GlobalPositionFinder`
has no catapult-specific scoring or proximity logic.

Military construction can also be triggered by event handlers, for example
after a new military building is occupied, after a tree is chopped, after a
building loses resources and is destroyed, or after land is lost. These paths
still use `AddBuildJob` with a radius search.

## Choosing The Military Building Type

`AIConstruction::ChooseMilitaryBuilding()` selects the building type before the
position search runs.

The current rules are:

- If no military building type is allowed, return no job.
- If the AI has fewer than five military buildings or military building sites,
  choose `Barracks`.
- Otherwise start from the smallest allowed military building type.
- Sometimes choose `Guardhouse` when the AI has low private count, or by random
  chance when stones are available or a quarry exists.
- If the anchor is close to a harbor position and sea attack is enabled, prefer
  `Watchtower`; if that cannot be built, choose the biggest allowed military
  building.
- If an enemy military building is within distance `35` of the anchor, choose a
  stronger building:
  - sometimes `Catapult`, if catapults are allowed and wanted
  - otherwise `Watchtower` or the biggest allowed military building
  - with a small random fallback to `Guardhouse`, so expansion can still happen
    when larger building spots are scarce

The helper functions `GetSmallestAllowedMilBuilding()` and
`GetBiggestAllowedMilBuilding()` scan `BuildingProperties::militaryBldTypes`
and test `AIInterface::CanBuildBuildingtype()`.

## Choosing The Actual Tile

`BuildJob::TryToBuild()` performs the position search.

For normal military building types (Barracks, Guardhouse, Watchtower, Fortress)
queued by `AddMilitaryBuildJob()`, the search mode is `SearchMode::Global`. The
job calls `aijh.FindBestPosition(type)` which delegates to
`GlobalPositionFinder::FindBestPosition(type)`.

`GlobalPositionFinder` scans all owned, reachable, non-farmed tiles. A
candidate is accepted when:

- the building quality can host the requested building size
- the tile is not too close to an empty harbor position
- military buildings may use reserved military border slots (non-military
  buildings cannot)
- the future house flag (`GetNeighbour(pt, Direction::SouthEast)`) is not
  already on a road
- the Borderland resource value at the tile is positive (used as the score; the
  `AIConfig` sets `AIResource::Borderland` as the resource rating for all four
  military types)

The computer-barrier filter that applies in local Borderland searches is
intentionally omitted for global military search, so the AI can place a
building inside the barrier when it is otherwise the best legal position.

`Catapult` still uses `SearchMode::Radius` and `FindPositionForBuildingAround`
with local Borderland candidate filters (including the computer-barrier check).

After a valid military spot is found, `BuildJob::TryToBuild()` may change the
building type (for both global and radius paths):

- If the current tile can support a larger military building and surrounding
  build space is scarce, the job upgrades to the next larger allowed military
  building.
- If no position was found, expansion is required, and the requested type is
  larger than a hut, the AI queues a smaller military building. For global
  jobs, `AddGlobalBuildJob(smallerType)` is used; for radius jobs,
  `AddBuildJob(smallerType, around)` is used.

For global military jobs, after a position is found, `CanStillConstructHere`
is checked against the found position (not the anchor). If another order placed
in the same execution wave is too close, the job fails rather than stacking
construction on or near the same best candidate.

After `SetBuildingSite()` succeeds, `AIConstruction::ConstructionOrdered()`
records the target point and increments the order counter for that building
type. Military buildings only get the main road; unlike non-military buildings,
they do not try to add a secondary road.

## Queueing And Deduplication

`AIConstruction::AddBuildJob()` treats military and non-military jobs
differently:

- military radius jobs are appended directly and can stack
- non-military radius jobs are deduplicated by `type + around`

This means the queue may contain several pending military jobs for the same or
nearby anchors. The later execution checks decide whether each one can still
place a site.

Global build jobs have a separate rule in `AIConstruction::AddGlobalBuildJob()`:
non-military global jobs are unique per building type, while military global
jobs are capped at three queued jobs per type. Normal expansion from
`AddMilitaryBuildJob()` uses radius jobs, not this global queue.

## Simultaneous Military Construction Limit

The primary limit is not the number of queued military jobs. It is the decision
made at execution time by:

```cpp
AIConstruction::Wanted(type)
BuildingPlanner::WantMoreMilitaryBlds(aijh, pendingMilitarySites)
```

For military building types, `Wanted()` computes
`AIConstruction::GetNumMilitaryConstructionOrders()` — the sum of
`constructionorders[bld]` for every military building type ordered in the
current execution wave — and passes it as `pendingMilitarySites` to
`WantMoreMilitaryBlds`.

`WantMoreMilitaryBlds()` first limits the number of active military building
sites, including orders placed earlier in the same wave:

```cpp
if(GetNumMilitaryBldSites() + pendingMilitarySites
   >= std::min(8u, GetNumMilitaryBlds() + pendingMilitarySites + 3))
    return false;
```

Important detail: `GetNumMilitaryBlds()` sums `GetNumBuildings(type)` for all
military types, and `GetNumBuildings(type)` includes both completed buildings
and building sites. `GetNumMilitaryBldSites()` counts only military sites.

The pending-sites argument prevents global military jobs executed in one pass
from collectively exceeding the cap before `BuildingPlanner` reflects the new
sites.

So the practical active-site cap is:

- at most `min(8, completed_or_site_military_count + pending + 3)` military
  building sites (where `pending` resets to 0 each network wavefront)
- once the AI has grown, this becomes a hard cap of `8` simultaneous military
  building sites

If the site cap has not been hit, the AI wants more military buildings when any
of these conditions is true:

- expansion is currently required
- at least one sawmill already exists
- boards are above `30` and a sawmill site exists
- otherwise, total military buildings plus military sites is below `3`

## Additional Throttles

Other checks can delay or prevent a military site even when the AI wants more:

- `AIConstruction::ExecuteJobs()` attempts at most five normal build jobs per
  execution pass, after connect jobs and global build jobs.
- `BuildJob::TryToBuild()` pauses if the player already has more than `40`
  building sites of all types.
- `AIConstruction::CanStillConstructHere()` refuses non-global jobs whose
  anchor is within distance `< 12` of a construction location recorded in the
  current network wavefront.
- `AIConstruction::ConstructionsExecuted()` clears those temporary construction
  locations and order counters on the next network wavefront.
- After a military target is selected, `BuildJob::ExecuteJob()` fails the job if
  another own military building is already too near the target.

## Timing

`AIPlayerJH::RunGF()` drives the loop:

- construction reservations are cleared on network wavefronts
- build jobs are executed every `100` game frames
- `PlanNewBuildings()` runs on the AI-level-dependent build interval:
  - Easy: `1000`
  - Medium: `400`
  - Hard: `200`

Because planning and execution are separate, queued military jobs can accumulate
between execution passes. The number that actually turns into construction
sites is controlled by the execution-time checks above.

## Queueing And Deduplication

`AIConstruction::AddGlobalBuildJob()` caps military global jobs at three queued
jobs per type. Normal expansion from `AddMilitaryBuildJob()` now uses this
global queue.

`AIConstruction::AddBuildJob()` allows multiple simultaneous military radius
jobs (no deduplication); non-military radius jobs are deduplicated by
`type + around`. Catapult placement still goes through `AddBuildJob`.
