# AI Global Build Job Cooldown Plan

## Goal

Add a per-`BuildingType` cooldown for failed **global** building-position
searches so the AI does not repeatedly run `GlobalPositionFinder` after it has
already proven that no valid spot exists.

This is a planning document only. No code changes are included here.

## Motivation

`GlobalPositionFinder::FindBestPosition()` scans the whole map and is one of the
most expensive AI planning operations. The existing negative cache only helps
until `InvalidateGlobalPositionCache()` is called, and that cache is invalidated
frequently by unrelated world updates such as new roads, buildings, border
changes, and terrain updates.

Late-game this means the AI can repeatedly rescan the whole map for the same
building type even when nothing meaningful has changed for that building.

## Current Relevant Flow

- `AIEconomyController::PlanNewBuildings()` and some event handlers enqueue
  global jobs via `AddGlobalBuildJob()`.
- `AIConstruction::ExecuteJobs()` pops global jobs and runs
  `BuildJob::ExecuteJob()`.
- `BuildJob::TryToBuild()` calls `aijh.FindBestPosition(type)` when
  `searchMode == SearchMode::Global`.
- `AIEconomyController::FindBestPosition()` forwards to
  `GlobalPositionFinder::FindBestPosition()`.
- If no point is found, the job fails immediately and later planning passes can
  enqueue the same global job again.

## Recommended Design

### Scope

Apply the cooldown only to `SearchMode::Global`.

Do not apply it to:

- `FindPositionForBuildingAround()`
- radius build jobs
- road connection retries
- failures that happen after a valid position was already found

The optimization target is unnecessary full-map scans, not all construction
retries.

### State Owner

Store the cooldown state in `AIConstruction`, because it already owns the global
build-job queue and already keeps other per-building planning state via
`helpers::EnumArray<..., BuildingType>`.

Suggested field:

```cpp
helpers::EnumArray<unsigned, BuildingType> nextGlobalSearchAllowedGF_{};
```

Recommended helper API in `AIConstruction`:

- `bool IsGlobalSearchOnCooldown(BuildingType type) const;`
- `void StartGlobalSearchCooldown(BuildingType type, unsigned durationGF);`

The current frame can be read cheaply from
`aijh.GetWorld().GetEvMgr().GetCurrentGF()`, so no new timing subsystem is
needed.

### Cooldown Semantics

- Start the cooldown only when a **global position search returns
  `MapPoint::Invalid()`**.
- Do not start it for:
  - `SetBuildingSite()` failure
  - BQ changes after placement
  - road-connection failure
  - destroyed flags/sites
- A cooldown is independent from the `GlobalPositionFinder` cache generation.
  `InvalidateGlobalPositionCache()` should not clear it, otherwise frequent
  invalidations would defeat the optimization.

Recommended initial duration:

- `500` gameframes

Why `500` is a good first value:

- Hard AI plans every `200` gf, so this suppresses roughly 2-3 plan cycles.
- Medium AI plans every `400` gf, so it suppresses roughly 1 retry cycle.
- Easy AI plans every `1000` gf, so it does not slow that level down further.

## Queue Behavior

Do not solve this by simply dropping `AddGlobalBuildJob()` requests while the
cooldown is active.

Reason: some global goals, especially `Storehouse`, are triggered by events and
are not part of the regular `globalBldToTest` loop. If the request is skipped at
enqueue time, the goal may never be retried after the cooldown expires.

Recommended behavior:

- keep the global job queued,
- when it reaches `BuildJob::TryToBuild()`, check the cooldown before calling
  `FindBestPosition(type)`,
- if the cooldown is still active, return without running the search,
- requeue the job without decrementing its priority.

That preserves intent while avoiding the expensive map scan.

## Concrete Implementation Steps

### 1. Add cooldown tracking to `AIConstruction`

Files:

- `libs/s25main/ai/aijh/planning/AIConstruction.h`
- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`

Changes:

- add `nextGlobalSearchAllowedGF_` storage,
- add helper methods for querying/starting cooldowns,
- define a local constant such as
  `constexpr unsigned kGlobalBuildSearchCooldownGF = 500;`

For v1, keep the value code-local and easy to tune. It can move into AI config
later if playtesting shows a need.

### 2. Gate the global search in `BuildJob::TryToBuild()`

File:

- `libs/s25main/ai/aijh/planning/Jobs.cpp`

Changes:

- before `foundPos = aijh.FindBestPosition(type);`, ask
  `aijh.GetConstruction().IsGlobalSearchOnCooldown(type)`,
- if true, return early without calling `FindBestPosition(type)`,
- leave the job in a retryable state instead of marking it failed.

This is the critical behavior change that actually removes the heavy scan.

### 3. Start cooldown only for true search misses

File:

- `libs/s25main/ai/aijh/planning/Jobs.cpp`

Changes:

- when `searchMode == SearchMode::Global` and `foundPos` is invalid, call
  `StartGlobalSearchCooldown(type, kGlobalBuildSearchCooldownGF)` before marking
  the job failed.

This keeps the optimization tightly scoped to the user-requested case:
"search failed to find any suitable position".

### 4. Preserve queued jobs without priority decay while blocked

File:

- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`

Changes:

- in the global-job branch of `ExecuteJobs()`, distinguish:
  - real execution failure/progress,
  - cooldown-blocked no-op retry
- when a job was only blocked by cooldown, reinsert it without `priority--`

Without this, a cooled-down job would still churn through the queue and lose
priority every cycle despite not actually doing any work.

### 5. Keep reset behavior intentionally simple

For the first implementation:

- let time expiry be the only reset mechanism,
- do not clear cooldowns from `InvalidateGlobalPositionCache()`,
- do not try to predict "relevant enough" world changes per building type yet.

This keeps the optimization robust and cheap. If playtesting later shows the AI
reacts too slowly after major land/resource changes, selective early-reset rules
can be added as a second step.

## Test Plan

Primary test target:

- `tests/s25Main/integration/testAI.cpp`

Recommended coverage:

1. Create a map/ownership situation where a chosen global building type cannot
   be placed at all.
2. Run the AI long enough for one global search miss.
3. Advance the game by less than the cooldown duration while continuing normal
   AI execution.
4. Verify the same global search is not retried during the cooldown window.
5. Advance past cooldown expiry and verify exactly one new retry happens.

Optional stronger test:

1. Start with no valid global position for a type.
2. Let the cooldown begin.
3. Change the world so a valid spot appears.
4. Verify the AI does not rescan until cooldown expiry.
5. After expiry, verify it performs the search and can place the building.

This test matches the intended tradeoff: a bounded delay in exchange for fewer
late-game full-map scans.

## Documentation Follow-Up

After implementation, update:

- `docs/ai/construction-mechanics.md`
- `docs/ai/position-finding.md`

Document that:

- global build jobs now have per-type retry throttling after a miss,
- the cooldown applies only to global searches,
- the `GlobalPositionFinder` cache still exists, but the new cooldown prevents
  repeated rescans across unrelated cache invalidations.

## Expected Outcome

The AI keeps the same strategic intent, but repeated "no position exists"
global build jobs stop triggering expensive full-map searches every few hundred
frames or after every cache invalidation. The biggest win should be in late-game
economies where many global building types are wanted but the map is already
fully saturated.
