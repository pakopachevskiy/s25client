# AI Construction Job Mechanics

See also:

- [position-finding.md](position-finding.md) — `FindBestPosition` /
  `FindPositionForBuildingAround` are the searches that feed the build
  queues.
- [road-route-selection.md](road-route-selection.md) — details for
  `ConnectFlagToRoadSystem`, `BuildAlternativeRoad`, and the road
  scoring referenced by the road/flag utilities here.
- [configuration.md](configuration.md) — `wantedParams`, `disableBuilding`,
  and `bqPenalty` knobs that gate `Wanted()` and route choices.
- [performance-profiling.md](performance-profiling.md) — `ExecuteAIJob`,
  `ExecuteGlobalBuildJobs`, `ExecuteBuildJobs`, and `ExecuteConnectJobs`
  cover this dispatch loop in profiler output.

## Job Queues and Deduplication
- `AddGlobalBuildJob` keeps a single global job per `BuildingType` for non-military types, so
  strategic one-off goals (e.g., “build a shipyard somewhere”) do not stack and spam the planner.
  Military global jobs are capped at three queued jobs per building type rather than one.
- Global jobs are ordered by `BuildJob::priority`. Full-map searches that fail to find a valid
  position are requeued with a large priority penalty, delaying expensive retries while keeping
  the strategic goal alive.
- `AddBuildJob` handles per-location work. It forbids invalid shipyard spots, allows multiple
  simultaneous military builds, but deduplicates non-military jobs by `type + around` so only
  one farmhouse, quarry, etc. is queued for a specific area at a time. Jobs can be pushed to
  the front of the deque when the caller wants immediate attention.
- `AddConnectFlagJob` ensures each flag enters the connection queue at most once, avoiding
  redundant “hook this node up” tasks.
- New building placement pauses once the AI has reached the configured active building-site
  cap. The effective cap is the lower of `AIConfig::maxBuildingSites`
  and available builder capacity plus `AIConfig::builderAdvance`; builder capacity counts
  existing builders and hammers that can create builders. Defaults are `maxBuildingSites: 40`
  and `builderAdvance.constant: 1`.

## Where `buildJobs` Entries Come From
- `AIPlayerJH::AddBuildJob` is the only regular gateway into the deque; it wraps the requested
  `BuildingType`, target point, and search mode into a `BuildJob` and forwards it to
  `AIConstruction::AddBuildJob` (optionally to the front of the queue for urgent orders).
- Strategic planners feed this entry point in multiple ways: direct planner calls, helper loops
  such as `AddBuildJobAroundEveryWarehouse/MilBld`, scripted expansion hooks like
  `AddMilitaryBuildJob`, and Lua automation that either force-places a site or, when `forced`
  is false, pends a normal build job after `Wanted()` confirms demand.
- Existing `BuildJob`s also recycle into the deque. Whenever construction fails (bad BQ, road
  connection impossible, destroyed flag, etc.) or needs to retry with a downgraded building,
  the job reissues itself by calling `aijh.AddBuildJob` with the original parameters.
- Follow-up chains replenish the deque: finishing certain economic buildings (farm → well,
  mill → bakery, pig farm → slaughterhouse, beer producers → well) enqueue downstream jobs so
  the economy stays balanced automatically.
- Event handlers (`HandleBuilingDestroyed`, loss of territory/resources, etc.) and building
  search jobs (`FindPositionForBuildingAround`, `SearchMode::Global`) all funnel through the
  same wrapper, so every AI stimulus still results in the same deduped `buildJobs` entries.

## Execution Order
- `ExecuteJobs(limit)` interleaves work from three queues: up to five global build jobs,
  followed by up to five location-specific build jobs, then connection jobs. Global and
  location-specific jobs are bounded by the supplied limit; connection jobs process up to
  the queue size captured at the start of the tick. This keeps AI cycles short while ensuring
  each queue makes forward progress every tick.
- Local build jobs and connect jobs that report `JobState::Finished` or `JobState::Failed`
  drop out; everything else is requeued at the back so transient blockers retry later.
- Global jobs live in an ordered multiset. Before a global job runs, its priority is decreased
  by 1. Global jobs that need another attempt are held aside until the current global-job cycle
  ends, so they cannot be selected repeatedly ahead of lower-priority queued jobs in the same
  cycle. After the global-job cycle, every queued global job gains the building type's configured
  `wantedParams.priorityInc` value, defaulting to 1 except for `Storehouse`, which defaults to 3,
  so jobs that were not selected gradually rise relative to recently attempted work.
- Global `BuildJob` failures carry a `BuildJobFailReason`. `NotWanted` jobs are dropped.
  `Shortage` jobs, such as active-site-cap or construction-material blockers, are requeued
  with an additional 2-point priority penalty. `NoValidPosition` jobs are requeued with an
  additional 40-point penalty, delaying repeated full-map scans when no suitable tile exists.

## Construction Reservation Tracking
- `constructionlocations` collects every point touched by orders during the current navigation
  wavefront. `ConstructionOrdered` pushes the build site, and helpers such as
  `SetFlagsAlongRoad` and successful road orders add intermediate tiles.
- `CanStillConstructHere` checks the list and refuses any new job whose target is within 12
  tiles of an active site, spacing projects out so carriers do not pile into the same area.
- `constructionorders` mirrors `BuildingType` values; once `ConstructionsExecuted` is called
  the counts reset. `Wanted()` compares these counters to the `BuildingPlanner` demand so the
  AI only spawns as many work orders as strategic planning requested.

## Building Periods
- The deterministic builder-only period starts after `noBuildingSite::GotWorker` receives a
  `nofBuilder`, assumes all construction wares are already available at the site, and excludes
  builder travel, carrier delivery, waiting for missing materials, and optional leveling.
- Medium, large, and harbor sites may first enter `BuildingSiteState::Planing` when neighboring
  terrain heights differ. That planer phase depends on worker travel and local terrain, so it is
  not part of the fixed periods below.
- `BUILDING_COSTS[type]` gives the number of boards and stones. Each ware provides 8 build
  steps through `nofBuilder::ChooseWare()`. Each step costs 17 GF of build freewalk plus 40 GF
  of hammering, and each ware chunk is followed by one 17 GF freewalk before either picking the
  next ware or completing the building. The first material pickup is preceded by a 24 GF waiting
  freewalk.
- Formula: `builderPeriodGF = 24 + (boards + stones) * (8 * (17 + 40) + 17)`, or
  `24 + (boards + stones) * 473`.

| Building type | Boards | Stones | Build steps | Builder period (GF) |
| --- | ---: | ---: | ---: | ---: |
| Barracks | 2 | 0 | 16 | 970 |
| Guardhouse | 2 | 3 | 40 | 2389 |
| Watchtower | 3 | 5 | 64 | 3808 |
| Vineyard | 4 | 4 | 64 | 3808 |
| Winery | 2 | 3 | 40 | 2389 |
| Temple | 4 | 7 | 88 | 5227 |
| Fortress | 4 | 7 | 88 | 5227 |
| GraniteMine | 4 | 0 | 32 | 1916 |
| CoalMine | 4 | 0 | 32 | 1916 |
| IronMine | 4 | 0 | 32 | 1916 |
| GoldMine | 4 | 0 | 32 | 1916 |
| LookoutTower | 4 | 0 | 32 | 1916 |
| Catapult | 4 | 2 | 48 | 2862 |
| Woodcutter | 2 | 0 | 16 | 970 |
| Fishery | 2 | 0 | 16 | 970 |
| Quarry | 2 | 0 | 16 | 970 |
| Forester | 2 | 0 | 16 | 970 |
| Slaughterhouse | 2 | 2 | 32 | 1916 |
| Hunter | 2 | 0 | 16 | 970 |
| Brewery | 2 | 2 | 32 | 1916 |
| Armory | 2 | 2 | 32 | 1916 |
| Metalworks | 2 | 2 | 32 | 1916 |
| Ironsmelter | 2 | 2 | 32 | 1916 |
| Charburner | 4 | 3 | 56 | 3335 |
| PigFarm | 3 | 3 | 48 | 2862 |
| Storehouse | 4 | 3 | 56 | 3335 |
| Mill | 2 | 2 | 32 | 1916 |
| Bakery | 2 | 2 | 32 | 1916 |
| Sawmill | 2 | 2 | 32 | 1916 |
| Mint | 2 | 2 | 32 | 1916 |
| Well | 2 | 0 | 16 | 970 |
| Shipyard | 2 | 3 | 40 | 2389 |
| Farm | 3 | 3 | 48 | 2862 |
| DonkeyBreeder | 3 | 3 | 48 | 2862 |
| HarborBuilding | 4 | 6 | 80 | 4754 |

## Road and Flag Utilities
- `FindFlags` gathers nearby valid `noFlag` objects for the player (wrapping-safe) so the AI
  knows where existing infrastructure lies.
- `ConnectFlagToRoadSystem` searches those candidates, rejecting options that lack a path,
  exceed two non-flaggable tiles in a row, or already link back to the origin. The winning
  route is fed into `MinorRoadImprovements/BuildRoad`, which issues the build order and
  records both end flags in `constructionlocations` so future commands avoid the corridor.
- `BuildAlternativeRoad` compares a proposed shortcut with the current path to the same flag
  and only commits if it substantially shrinks the travel length. `IsConnectedToRoadSystem`
  and `FindTargetStoreHouseFlag` provide quick checks to see whether a flag is already tied
  into the network via the closest warehouse.

## Choosing What to Build
- `GetSmallestAllowedMilBuilding` / `GetBiggestAllowedMilBuilding` scan capability tables so
  later decisions know which blueprints are legal at the moment.
- `ChooseMilitaryBuilding` factors in garrison counts, stone supply, harbor pressure, nearby
  enemies, and a bit of randomness to break ties. Special cases allow catapult pushes or
  fallback to guardhouses when expansion space is tight.
- `Wanted()` gates every non-road job: it consults `BuildingPlanner` demand, current
  inventory (e.g., sawmills need sufficient wood), and rejects forbidden options such as
  catapults when the technology is locked.
- Helper counts like `CountUsualBuildingInRadius`, `OtherUsualBuildingInRadius`, and
  `OtherStoreInRadius` ensure the planner does not seed multiple identical buildings on top
  of each other before previous jobs finish.
