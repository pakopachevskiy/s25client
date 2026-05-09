# Military Global Position Search Plan

## Goal

Switch new AI military building placement from local radius search to
`GlobalPositionFinder`, while preserving the current logic that chooses which
military building type to build.

The intended behavior is:

- `AIConstruction::ChooseMilitaryBuilding(anchor)` still decides the requested
  military building type.
- Normal military buildings then use `SearchMode::Global`, so their final tile
  comes from `GlobalPositionFinder::FindBestPosition(type)`.
- `Catapult` remains on the old local radius path unless explicit global
  catapult scoring is added.
- Existing military type upgrade/downgrade behavior is kept for global military
  searches.
- Same-wave military construction orders are counted when deciding whether more
  military sites may be started.
- Global military search preserves the important local military placement
  filters, except the local computer-barrier rejection is intentionally omitted
  for global search.

## Relevant Code

- `libs/s25main/ai/aijh/runtime/AIEconomyController.cpp`
  - `AIEconomyController::AddMilitaryBuildJob`
  - `AIEconomyController::AddGlobalBuildJob`
  - `AIEconomyController::AddBuildJob`
- `libs/s25main/ai/aijh/planning/AIConstruction.cpp`
  - `AIConstruction::ChooseMilitaryBuilding`
  - `AIConstruction::Wanted`
  - `AIConstruction::ConstructionOrdered`
- `libs/s25main/ai/aijh/planning/BuildingPlanner.cpp`
  - `BuildingPlanner::WantMoreMilitaryBlds`
  - `BuildingPlanner::GetNumMilitaryBlds`
  - `BuildingPlanner::GetNumMilitaryBldSites`
- `libs/s25main/ai/aijh/planning/Jobs.cpp`
  - `BuildJob::TryToBuild`
- `libs/s25main/ai/aijh/planning/GlobalPositionFinder.cpp`
  - `GlobalPositionFinder::FindBestPosition`
- `libs/s25main/ai/aijh/runtime/AIResourceMap.cpp`
  - current local `Borderland` candidate filters used by radius placement
- `libs/s25main/ai/aijh/config/AIConfig.cpp`
  - default `Borderland` resource rating for military buildings

## Current Behavior Summary

`AIEconomyController::PlanNewBuildings()` calls
`AddMilitaryBuildJob(anchor)` around a warehouse and around a random existing
military building.

`AddMilitaryBuildJob(anchor)` currently:

1. calls `AIConstruction::ChooseMilitaryBuilding(anchor)`
2. queues `AddBuildJob(type, anchor, false, true)`
3. later `BuildJob::TryToBuild()` uses `SearchMode::Radius`
4. military radius search calls
   `FindPositionForBuildingAround(type, anchor)`
5. normal military buildings search radius `11` around the anchor using
   `AIResource::Borderland`

`GlobalPositionFinder` already has default `Borderland` resource ratings for
`Barracks`, `Guardhouse`, `Watchtower`, and `Fortress`, so these building types
can already use global search.

## Implementation Steps

### 1. Route Normal Military Jobs To Global Search

Change `AIEconomyController::AddMilitaryBuildJob(MapPoint pt)`.

Keep the existing type choice:

```cpp
const auto milBld = owner_.construction->ChooseMilitaryBuilding(pt);
```

Then branch by selected type:

- if `BuildingProperties::IsMilitary(*milBld)`, call
  `AddGlobalBuildJob(*milBld)`
- otherwise call `AddBuildJob(*milBld, pt, false, true)`

This preserves `ChooseMilitaryBuilding()` behavior while changing the final
location selection for normal military buildings to global search.

Keep `Catapult` on the local radius path because:

- `BuildingProperties::IsMilitary(Catapult)` is false
- `GlobalPositionFinder` has no catapult-specific default resource rating or
  proximity logic
- current catapult placement has local spacing checks in
  `FindPositionForBuildingAround()`

### 2. Extract Military Search Adjustment From Radius-Only Branch

`BuildJob::TryToBuild()` currently applies military-specific post-search logic
inside the `SearchMode::Radius` branch:

- upgrade to a larger military building if the found tile supports it and
  surrounding build space is scarce
- when no position is found and expansion is required, queue a smaller military
  building

After normal military jobs switch to `SearchMode::Global`, that logic would no
longer run.

Refactor this into helper functions near `BuildJob::TryToBuild()`:

- `MaybeUpgradeMilitaryBuildingForPosition(MapPoint foundPos)`
- `QueueSmallerMilitaryBuildingIfUseful()`

Then call the shared logic after the search-mode switch:

1. perform global, radius, or none search
2. if `BuildingProperties::IsMilitary(type)`:
   - if `foundPos.isValid()`, apply upgrade logic
   - otherwise, apply downgrade retry logic
3. continue with the existing invalid-position handling

Keep the downgrade behavior scoped to meaningful retries:

- only when expansion is required
- only when the requested building is larger than `BuildingQuality::Hut`
- only when a smaller allowed military type exists

Make the downgrade retry search-mode aware:

- if the current job is `SearchMode::Global`, queue the smaller military type
  with `aijh.AddGlobalBuildJob(bld)`
- if the current job is `SearchMode::Radius`, keep the current local retry
  behavior with `aijh.AddBuildJob(bld, around)`
- do not use `AddBuildJob(bld, around)` for a global job because global jobs
  have `around == MapPoint::Invalid()`

### 3. Add Global Military Candidate Filters

Routing military jobs to `GlobalPositionFinder` is not enough by itself. The
local `Borderland` resource-map search currently rejects candidates whose future
house flag is already on a road, candidates inside the computer barrier, and
candidates too close to empty harbor positions.

For global military placement, preserve the important placement filters but
intentionally omit the computer-barrier rejection:

- reject candidates whose future house flag is already on a road
- reject candidates too close to empty harbor positions
- keep the existing global ownership, reachability, farmed-node, and building
  quality checks
- keep the existing global special handling that allows military buildings to
  use reserved military border slots
- do not reject `world.IsInsideComputerBarrier(pt)` for global military search

Add the filtering in `GlobalPositionFinder`, preferably as a small helper such
as:

```cpp
bool IsMilitaryGlobalCandidateBlocked(const AIWorldView& aijh,
                                      const AIQueryService& queries,
                                      BuildingType type,
                                      MapPoint pt);
```

Call it from `GlobalPositionFinder::FindBestPosition()` after the common
ownership/building-quality/harbor checks and before rating the point.

If possible, avoid duplicating logic by extracting shared candidate predicates
from the local `Borderland` search. If that would make the change too wide,
duplicate the small checks in the global finder and document the intentional
difference: global military search does not apply the computer-barrier filter.

### 4. Enforce Target-Based Construction Reservation

The old local radius path applies `CanStillConstructHere(around)` before the
search. Global jobs have no meaningful anchor, so the spacing check must move to
the selected target for global military construction.

After a global military search returns `foundPos`, and before
`SetBuildingSite(foundPos, type)`, require:

```cpp
if(searchMode == SearchMode::Global && BuildingProperties::IsMilitary(type)
   && !aiConstruction.CanStillConstructHere(foundPos))
{
    state = JobState::Failed;
    return;
}
```

This prevents multiple global military jobs in the same execution wave from
placing sites on or near the same best stale candidate. Keep the existing
pre-search `CanStillConstructHere(around)` check for non-global jobs.

### 5. Mitigate Same-Wave Military Site Overshoot

`AIConstruction::Wanted(type)` currently returns
`bldPlanner.WantMoreMilitaryBlds(aijh)` for every military building type. This
checks existing military building sites, but it does not account for military
sites ordered in the current network wavefront and not yet reflected in
`BuildingPlanner`.

Because global jobs do not use a local anchor and multiple military global jobs
can execute in one pass, include current-wave military construction orders in
the military site cap decision.

Add an AI construction helper, for example:

```cpp
unsigned AIConstruction::GetNumMilitaryConstructionOrders() const;
```

It should sum `constructionorders[bld]` for every
`BuildingProperties::militaryBldTypes` entry.

Then update the military branch of `AIConstruction::Wanted(type)` so it applies
the site cap with pending current-wave orders included.

One low-impact shape:

```cpp
if(BuildingProperties::IsMilitary(type))
{
    const unsigned currentSites = bldPlanner.GetNumMilitaryBldSites();
    const unsigned currentMilitary = bldPlanner.GetNumMilitaryBlds();
    const unsigned pendingSites = GetNumMilitaryConstructionOrders();
    if(currentSites + pendingSites >= std::min(8u, currentMilitary + pendingSites + 3))
        return false;
    return bldPlanner.WantMoreMilitaryBlds(aijh);
}
```

This preserves the existing economic gates in `WantMoreMilitaryBlds()` while
making the immediate site cap aware of orders that were just placed.

Preferred cleaner change:

- change `BuildingPlanner::WantMoreMilitaryBlds()` to accept an optional
  `pendingMilitarySites` argument
- keep all military site-cap logic in `BuildingPlanner`
- have `AIConstruction::Wanted()` pass
  `GetNumMilitaryConstructionOrders()`

Prefer this version so military site-cap logic stays in one place.

### 6. Review Global Queue Semantics

`AIConstruction::AddGlobalBuildJob()` currently caps military global jobs at
three queued jobs per type.

This is probably acceptable for the first change because:

- military planning can enqueue jobs frequently
- `Wanted()` gates execution
- the active site cap prevents large bursts

After implementation, verify whether the per-type global cap of three interacts
poorly with type selection. Example: if `ChooseMilitaryBuilding()` keeps
choosing `Barracks`, only three barracks global jobs can queue before execution
processes them.

Do not change this cap in the first pass unless tests or manual inspection show
starvation.

### 7. Add Tests Or Focused Coverage

Look for existing AI construction tests first. If direct tests are practical,
add coverage for:

- `AddMilitaryBuildJob()` queues a global build job for `Barracks`,
  `Guardhouse`, `Watchtower`, or `Fortress`.
- `AddMilitaryBuildJob()` keeps `Catapult` on the radius path.
- military `Wanted()` returns false when existing sites plus current-wave
  military orders hit the cap.
- military post-search upgrade logic runs for global search results.
- military downgrade retry can still queue a smaller building after a failed
  global search during required expansion, using `AddGlobalBuildJob()` rather
  than a radius job with an invalid anchor.
- global military search rejects candidates whose future flag is already on a
  road.
- global military search does not reject candidates solely because they are
  inside the computer barrier.
- global military jobs reject a found target that fails
  `CanStillConstructHere(foundPos)`.

If the existing test harness makes direct AI queue inspection difficult, add
smaller unit-level tests around extracted helper logic and document any
remaining manual verification.

### 8. Update Documentation

Update:

- `docs/ai/military-construction.md`
- `docs/ai/construction-mechanics.md`
- optionally `docs/ai/position-finding.md`

Document that normal military building jobs now use global position search, but
`Catapult` remains radius-based unless explicit global catapult scoring is
introduced.

Also update the global-job queue documentation in
`docs/ai/construction-mechanics.md`: non-military global jobs are unique per
type, while military global jobs are capped at three queued jobs per type.

Also fix or call out the existing global cooldown mismatch:

- docs currently mention `500` game frames
- code uses `kGlobalBuildSearchCooldownGF = 1000`

## Verification Checklist

Run at least:

```sh
cmake --build build --target s25client
ctest --test-dir build --output-on-failure
```

If a full local build is too expensive, run the narrowest available test target
covering AI construction and at least compile `libs/s25main`.

Manual behavior checks:

- Start an AI game and confirm new military sites appear on good borderland
  plots, not only near the randomly selected anchor.
- Confirm early expansion still starts with `Barracks`.
- Confirm enemy-adjacent anchors can still influence type choice toward larger
  military buildings or catapults.
- Confirm catapults still appear only through the local placement path.
- Confirm the AI does not create more than the intended military site cap in a
  single execution wave.
- Confirm global military sites do not reuse an already-roaded future flag
  position.
- Confirm global military search can still choose a valid candidate inside the
  computer barrier when it is otherwise the best legal position.

## Expected Risks

- Global military jobs may concentrate on the best borderland area instead of
  spreading around separate anchors. This is intended to some extent, but watch
  for over-concentration.
- Type choice still depends on the original anchor, while placement becomes
  global. A warehouse near an enemy or harbor can influence a building type that
  is ultimately placed elsewhere. This preserves existing type logic as
  requested, but may produce surprising choices.
- Global search is more expensive than radius search. Existing global-search
  cooldown and profiling should be watched after the change.
- The old local `CanStillConstructHere(anchor)` spacing check changes to a
  target-based check for global military jobs. This should prevent same-wave
  clustering around stale best candidates, but it may also reject a globally
  optimal site and defer construction until a later pass.
