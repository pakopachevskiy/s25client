# Sector Focus Strategy Implementation Plan

This document expands the earlier `Sector Focus` idea into a concrete
implementation plan for the current AI combat stack.

The goal is to make the AI keep pressure on one local frontier sector for a
short period instead of selecting each attack target independently from the
entire border every time `TryToAttack()` runs.

## 1. Objective

### Behavior we want

- Once the AI starts attacking in one part of the frontier, it should prefer to
  continue attacking nearby targets for the next few attack cycles.
- The AI should still abandon that sector when:
  - no viable targets remain there
  - the local force balance turns unfavorable
  - it has already spent too long on that flank with no progress
- This should be a targeting-layer feature, not a rewrite of combat execution.

### Why this fits the current codebase

- `AICombatController` already owns target-selection mode dispatch.
- `TryToAttack()` already calls `TrackCombatStart()` after a successful attack.
- The AI already maintains short-lived military memory in
  `AIMilitaryLogistics::recentlyLostBuildings_`.
- Candidate discovery is already centralized in
  `AICombatController::GetPotentialTargets()`.

That means `Sector Focus` can be implemented as:

1. new short-lived state in the combat controller
2. a new selection mode or a sector-bias layer around an existing mode
3. minimal plumbing in config and constructor setup

## 2. Design Choice

There are two viable designs.

### Option A: standalone mode

Add `SectorFocus` as a new `TargetSelectionMode`.

Pros:

- easy to configure and reason about
- isolated behavior for testing
- minimal interaction risk with existing modes

Cons:

- duplicates some evaluation logic already present in `Prudent` or `Biting`

### Option B: decorator over an existing base mode

Keep a base selector such as `Prudent`, then apply sector bias before the final
pick.

Pros:

- stronger reuse
- easier to combine with later modes

Cons:

- more code reshaping now
- current selectors return a single target, not ranked survivors

### Recommended first implementation

Use **Option A** first, but structure the code so it can later be converted into
“base selector + sector bias”.

Concretely:

- add `TargetSelectionMode::SectorFocus`
- implement it closest to `Prudent`
- keep the scoring code factored into helpers so later composition stays easy

## 3. Sector Model

The main design question is what a “sector” means.

### Recommended definition

A sector is a local cluster centered around a target position and represented by
an anchor `MapPoint`.

Two targets belong to the same sector when their positions are within a
configurable radius of that anchor.

This is better than trying to build full frontier clusters immediately because:

- it is cheap
- it matches the current local search style
- it does not require persistent graph partitioning of the frontier

### Initial sector state

Add a small state object in `AICombatController`:

```cpp
struct FocusSector
{
    MapPoint anchor;
    unsigned startedGf = 0;
    unsigned lastAttackGf = 0;
    unsigned successfulAttacks = 0;
    unsigned attempts = 0;
    bool active = false;
};
```

This is enough for v1.

### Lifetime rules

The focus sector should expire when any of these is true:

- `currentGF - lastAttackGf > sectorLifetime`
- no viable candidate remains within `sectorRadius`
- `attempts >= maxAttemptsWithoutProgress`
- local strength ratio in the sector falls below a threshold

For v1, “progress” can simply mean “we launched another attack in this sector”.
Later it can be upgraded to actual capture confirmation.

## 4. Data Flow And Hooks

### Existing hook we can reuse

`AICombatController::TryToAttack()` already calls:

```cpp
owner_.TrackCombatStart(*target);
```

This call is currently used only by stats reporting, so it is a good place to
also refresh sector memory.

### Recommended ownership

Keep focus-sector state inside `AICombatController`, not `AIPlayerJH` or
`AIMilitaryLogistics`.

Reason:

- the feature is specific to target selection
- it avoids widening the cross-module API too early
- it keeps the initial implementation local to combat code

### Minimal new hook

Add a private method to `AICombatController`:

```cpp
void NoteAttackLaunched(const nobBaseMilitary& target);
```

Then call it inside `TryToAttack()` right next to `TrackCombatStart()`.

This method should:

- activate the focus sector if none exists
- refresh `lastAttackGf`
- move the anchor only if the chosen target is far enough from the current
  anchor to justify a new sector
- increment `attempts`

## 5. Candidate Evaluation Strategy

### High-level algorithm

`SectorFocus` should run in four stages:

1. Build the normal potential target list with `GetPotentialTargets()`.
2. Filter to targets that are tactically viable.
   This should reuse the same safety checks as `Prudent`.
3. Partition candidates into:
   - `inSector`
   - `outOfSector`
4. If `inSector` contains acceptable targets, pick from it.
   Otherwise either:
   - relax and pick the best `outOfSector` target
   - or clear the sector and re-run globally

### Base tactical filter

For v1, copy the conservative rules from `Prudent`:

- skip buildings already under attack
- require at least one available attacker
- require `attackersCount >= defenders + 2`
- estimate enemy reserve near target and prefer lower values
- keep the existing defense-mode restrictions

This avoids introducing sector persistence on top of reckless attacks.

### Sector bias

Within the viable target set:

- first prefer candidates within `sectorRadius` of the active sector anchor
- if several remain, rank them using a prudent-style score
- if none remain, score all viable targets globally and decide whether to
  switch sectors

### Suggested v1 score

Inside the preferred sector:

```text
score =
  + 4.0 if in active sector
  + 2.0 if within close radius of last attacked target
  - 2.0 * defenderCount
  - 1.5 * estimatedEnemyReserve
  + 1.0 * availableAttackerMargin
```

Outside the preferred sector:

```text
score =
  - sectorSwitchPenalty
  - 2.0 * defenderCount
  - 1.5 * estimatedEnemyReserve
  + 1.0 * availableAttackerMargin
```

The key is not the exact weights. The important part is that a decent target in
the active sector beats a slightly better target elsewhere.

## 6. When To Switch Sectors

This is the core behavioral decision.

### Recommended rules

Switch away from the active sector when one of these holds:

1. No viable target remains in the sector.
2. The best in-sector target is much worse than the best global target.
3. The sector has gone stale.
4. The AI recently entered defense mode and the sector is not a retake sector.

### Concrete threshold

Use a configurable comparison:

```text
switch if bestGlobalScore > bestSectorScore + sectorSwitchThreshold
```

That prevents pathological tunnel vision.

### Retake exception

If the active sector is near a recently lost military building, give it a
temporary retention bonus even if the raw score is only slightly worse. This
matches the existing philosophy used by `CanAttackInDefenseMode()`.

## 7. File-By-File Change Plan

### `libs/s25main/ai/aijh/combat/AICombatContext.h`

Add the new enum value:

```cpp
enum class AICombatTargetSelectionMode
{
    Random,
    Prudent,
    Biting,
    Attrition,
    SectorFocus
};
```

No interface expansion is required for v1 if sector state stays local to
`AICombatController`.

### `libs/s25main/ai/aijh/config/AIConfig.h`

Add the matching config enum value:

```cpp
enum class TargetSelectionAlgorithm
{
    Random,
    Prudent,
    Biting,
    Attrition,
    SectorFocus
};
```

Extend `CombatConfig` with a nested settings block, for example:

```cpp
struct SectorFocusConfig
{
    unsigned radius = 14;
    unsigned closeRadius = 7;
    unsigned lifetime = 1800;
    unsigned maxAttemptsBeforeReset = 4;
    double switchPenalty = 3.0;
    double switchThreshold = 2.0;
};
```

Then add:

```cpp
SectorFocusConfig sectorFocus;
```

inside `CombatConfig`.

### `libs/s25main/ai/aijh/config/AIConfig.cpp`

Update the string parser so:

- `"sectorfocus"`
- `"sector_focus"`
- optionally `"sector-focus"`

all map to `TargetSelectionAlgorithm::SectorFocus`.

Parse an optional YAML block:

```yaml
combat:
  targetSelection: SectorFocus
  sectorFocus:
    radius: 14
    closeRadius: 7
    lifetime: 1800
    maxAttemptsBeforeReset: 4
    switchPenalty: 3.0
    switchThreshold: 2.0
```

### `libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp`

Extend constructor setup:

```cpp
case TargetSelectionAlgorithm::SectorFocus:
    combatController_->SetTargetSelectionMode(
      AICombatController::TargetSelectionMode::SectorFocus);
    break;
```

### `libs/s25main/ai/aijh/combat/AICombatController.h`

Add:

- `SelectAttackTargetSectorFocus() const`
- helper methods for sector state maintenance
- mutable focus-sector state

Suggested additions:

```cpp
const nobBaseMilitary* SelectAttackTargetSectorFocus() const;

struct FocusSector
{
    MapPoint anchor;
    MapPoint lastTarget;
    unsigned startedGf = 0;
    unsigned lastAttackGf = 0;
    unsigned attempts = 0;
    bool active = false;
};

void NoteAttackLaunched(const nobBaseMilitary& target);
void ResetFocusSector();
bool IsTargetInFocusSector(const nobBaseMilitary& target) const;
bool IsFocusSectorExpired() const;
```

Because `TryToAttack()` and selection helpers need to mutate state, either:

1. remove `const` from selector methods that manage focus state, or
2. mark the focus-sector state as `mutable`

Recommendation: use **non-const** only where mutation is real. The current API
is mostly `const`, but `Sector Focus` introduces genuine runtime state, so it
is better to acknowledge that instead of hiding it behind `mutable`
everywhere.

### `libs/s25main/ai/aijh/combat/TargetSelector.cpp`

Extend dispatch in `ResolveSelector()` for the new mode.

Potentially this file will need a small signature change if selector methods
stop being `const`.

### New file: `libs/s25main/ai/aijh/combat/TargetSelectorSectorFocus.cpp`

Create a dedicated implementation file rather than extending
`TargetSelectorPrudent.cpp`.

That file should contain:

- a local `CandidateScore` struct
- helper to compute prudent-style tactical viability
- helper to compute sector affinity
- final selection logic

### `libs/s25main/ai/aijh/combat/AICombatController.cpp`

Update `TryToAttack()`:

- after successful `commands.Attack(...)`
- call `NoteAttackLaunched(*target)`
- then call `owner_.TrackCombatStart(*target)` as before

Also add:

- `ResetFocusSector()`
- `IsFocusSectorExpired()`
- any lightweight helper functions shared by the sector selector

### Optional later cleanup

If `Prudent` and `SectorFocus` share too much code, extract a common helper such
as:

```cpp
std::vector<RankedTarget> BuildPrudentCandidates(...) const;
```

That refactor does not need to block v1.

## 8. Selector Pseudocode

The first implementation should look roughly like this:

```text
SelectAttackTargetSectorFocus():
  prune or clear expired focus sector
  candidates = GetPotentialTargets(...)
  viable = []

  for target in candidates:
    if not tactically viable:
      continue
    viable.push(target with computed score fields)

  if viable empty:
    reset focus sector
    return null

  inSector = viable targets within active sector radius

  if active sector exists and inSector not empty:
    bestSector = best scored target in inSector
    bestGlobal = best scored target in viable
    if bestGlobal.score > bestSector.score + switchThreshold:
      activate sector at bestGlobal.target
      return bestGlobal.target
    return bestSector.target

  bestGlobal = best scored target in viable
  activate sector at bestGlobal.target
  return bestGlobal.target
```

This gives the AI continuity without trapping it forever.

## 9. Expiry And Maintenance Details

### Expiry cadence

The simplest place to enforce expiry is inside the selector itself:

- before evaluating candidates, call `IsFocusSectorExpired()`
- if expired, `ResetFocusSector()`

That avoids adding yet another per-frame maintenance path in `RunGF()`.

### What counts as “attempts”

For v1:

- increment `attempts` on each successful `commands.Attack(...)`

Later, if better telemetry appears, split this into:

- `attacksLaunched`
- `capturesWon`
- `capturesLost`

### Anchor updates

Do not move the anchor on every attack.

Instead:

- keep the original anchor while attacks remain near it
- if the selected target lies outside `radius / 2`, reset the anchor to that
  target and restart the sector timer

This prevents the focus area from drifting too quickly across the map.

## 10. Testing Plan

The feature is stateful, so unit tests matter.

### Test targets

Add tests near the combat selector tests location. If there is no dedicated
combat selector suite yet, add one under:

- `tests/s25Main/ai/`
- or the nearest existing `AIPlayerJH` combat-related test area

### Minimum cases

1. No active sector
   The selector should pick the best viable global target and activate a
   sector around it.

2. Active sector with viable targets
   The selector should prefer an in-sector target over a slightly better
   out-of-sector target.

3. Active sector exhausted
   When no viable targets remain in-sector, the selector should switch.

4. Sector expiry by lifetime
   A stale sector should be cleared automatically.

5. Defense mode interaction
   Sector preference must not bypass `CanAttackInDefenseMode()`.

6. Hard AI strength interaction
   Sector preference must not bypass the existing hard-difficulty strength
   gate.

7. HQ/harbor opportunism
   If an HQ target is globally decisive, sector stickiness should not suppress
   it unless explicitly intended by config.

### Useful integration scenarios

- a border with two enemy flanks where one flank becomes weak after the first
  capture
- a sector that becomes dangerous because enemy reserve grows nearby
- recent-loss situations where the AI should keep pressure near the lost area

## 11. Risk Areas

### Risk 1: tunnel vision

The AI may keep attacking the same area even when a much better opening appears
elsewhere.

Mitigation:

- `switchThreshold`
- lifetime limit
- max attempts before reset

### Risk 2: duplicate prudent logic

`SectorFocus` may copy too much from `Prudent`.

Mitigation:

- accept duplication in v1 if it keeps the patch understandable
- extract shared helpers only after behavior is stable

### Risk 3: hidden mutable state in const APIs

The current selector methods are `const`, but the new behavior is stateful.

Mitigation:

- prefer explicit non-const methods where state actually changes
- keep pure scoring helpers `const`

### Risk 4: no direct success/failure feedback

`TrackCombatStart()` records attack start, not capture success.

Mitigation:

- v1 uses time and availability-based expiry only
- v2 can listen for building capture/loss events and reinforce or clear sectors

## 12. Recommended Implementation Phases

### Phase 1: plumbing and state

- add enums
- add config parsing
- add focus-sector state to `AICombatController`
- update mode dispatch and constructor selection

Deliverable:

- compiles with `SectorFocus` available but behavior may still fall back to
  prudent-style global selection

### Phase 2: basic sector-biased selector

- implement `SelectAttackTargetSectorFocus()`
- reuse prudent-style tactical filters
- bias toward in-sector candidates
- switch only when the sector is empty or stale

Deliverable:

- visible multi-attack continuity on one frontier

### Phase 3: smarter switching

- compare best in-sector vs best global candidate
- add `switchThreshold`
- add recent-loss retention bonus

Deliverable:

- less tunnel vision, more robust flank selection

### Phase 4: cleanup and tests

- extract shared helpers if warranted
- add focused unit tests
- document the new mode in `docs/ai/attack-target-selection.md`

## 13. Recommendation Summary

The cleanest first version is:

- new standalone `SectorFocus` target-selection mode
- local sector memory in `AICombatController`
- prudent-style tactical filters
- radius-based sector membership
- time-based expiry plus score-based switching

That gives the AI a noticeable new behavior pattern with limited code churn and
without destabilizing the combat execution pipeline.
