# Target Selection Proposals

This note proposes additional AI attack-target selection strategies beyond the
current `Random`, `Prudent`, `Biting`, and `Attrition` modes described in
`docs/ai/attack-target-selection.md`.

The current selector already gives us a good base:

- bounded candidate discovery from frontier military buildings
- attacker counts and aggregate attack strength near a target
- defender counts
- defense-mode gating
- `EstimateCaptureLossCount()` for structural damage
- recapture metadata (`GetOriginOwner`, `GetCapturedGF`)
- local enemy density queries via nearby military-building scans

The proposals below try to reuse those signals so they can be implemented as
new scoring modes instead of requiring a large combat-system rewrite.

## 1. Breakthrough

### Goal

Prefer attacks that are likely to open the frontier and create follow-up
opportunities, not just win the immediate building.

### Why it is different

- `Biting` focuses on collateral destruction after a capture.
- `Prudent` focuses on low defenders and low counterattack risk.
- `Breakthrough` would instead optimize for map position and future attack
  expansion.

### Suggested score

```
score =
  5.0 * newlyAttackableEnemyBuildingsAfterCapture
  + 3.0 * enemyFrontierPressureReduced
  + 2.0 * followUpTargetsWithinBaseRange
  - 2.0 * enemyReserveNearTarget
  - 1.0 * travelDistanceFromAttackers
```

### Implementation sketch

- Start from the current viable candidate list.
- For each target, estimate whether capturing it would expose additional enemy
  military buildings within `BASE_ATTACKING_DISTANCE` of the new frontier.
- Approximate the gain cheaply:
  - count enemy military buildings near the target that are currently blocked
    by this building’s zone of control
  - count own military buildings that would become frontline or near-frontline
  - reduce the score when the target sits under heavy enemy reserve coverage
- Keep the normal hard-difficulty strength checks.

### Expected behavior

- Stronger at carving corridors into clustered enemy borders.
- Better than `Prudent` when the best move is not the weakest garrison but the
  one that unlocks multiple future attacks.

## 2. Overmatch

### Goal

Attack only where local superiority is overwhelming, maximizing reliability and
 minimizing stalled offensives.

### Why it is different

- `Prudent` minimizes defenders first.
- `Overmatch` explicitly ranks by surplus force ratio and local reserve margin.

### Suggested score

```
score =
  4.0 * (ownAttackStrength - requiredDefenseStrength)
  + 3.0 * (ownAvailableAttackers - defenders)
  - 3.0 * enemyReserveNearTarget
  - 1.5 * targetDefenders
```

### Implementation sketch

- Reuse the current attacker aggregation from `GetSoldiersStrengthForAttack`.
- Estimate enemy reserve as nearby enemy soldiers beyond the main garrison,
  similar to `Prudent`, but use it as a stronger penalty.
- Require a configurable minimum margin before accepting the target:
  - `minStrengthLead`
  - `minSoldierLead`
- Pick the candidate with the largest local margin instead of the smallest
  defender count.

### Expected behavior

- More conservative than `Random` and `Biting`.
- Useful for medium AI personalities that should avoid coin-flip attacks.
- Likely to reduce the number of attacks that succeed tactically but leave the
  AI overextended immediately afterward.

## 3. Sector Focus

### Goal

Stop spreading attacks across the whole frontier and instead concentrate on one
 local sector until it weakens or collapses.

### Why it is different

- Current modes are largely stateless per attack interval.
- `Sector Focus` introduces short-lived memory so the AI keeps pressure on one
  flank.

### Suggested score

```
score =
  4.0 * inPreferredSector
  + 2.0 * sameSectorAsRecentAttack
  + 2.0 * ownLocalForceDensity
  - 2.0 * enemyLocalForceDensity
  - 1.0 * sectorSwitchPenalty
```

### Implementation sketch

- Partition targets into coarse sectors by map direction or by nearest own
  frontier building cluster.
- Store a short-lived `preferredAttackSector` and refresh it every N attacks or
  when the sector no longer has viable targets.
- While a sector is active, favor targets in that area unless another target is
  dramatically better.
- Combine with any base selector:
  - `Prudent + Sector Focus`
  - `Biting + Sector Focus`

### Expected behavior

- Creates more coherent offensives.
- Helps the AI finish a breach instead of poking three unrelated enemy posts.
- Should synergize well with follow-up captures after a successful attack.

## 4. Retaliation

### Goal

Punish the enemy building that is most responsible for recent territorial loss
 or immediate pressure on the AI frontier.

### Why it is different

- `Attrition` likes recaptures, especially recent ones.
- `Retaliation` is broader: it targets the enemy source of damage, even if the
  exact target was not originally ours.

### Suggested score

```
score =
  5.0 * recentlyCapturedOurBuilding
  + 4.0 * threatensMultipleOwnFrontierBuildings
  + 3.0 * closeToRecentlyLostArea
  - 2.0 * enemyReserveNearTarget
  - 1.0 * defenderCount
```

### Implementation sketch

- Keep a short history of:
  - own military buildings lost recently
  - map positions where enemy captures happened
- Score enemy targets by proximity to those losses.
- Boost buildings that project pressure onto multiple own frontier posts
  (count own frontier buildings within range of the target).
- Fall back to `Prudent` if no recent hostile activity exists.

### Expected behavior

- Makes the AI look less passive after losing ground.
- Better than pure recapture logic when the correct answer is to hit the nearby
  supporting fort rather than the exact building that changed hands.

## 5. Decapitation

### Goal

Seek operationally critical non-military targets when the enemy is already
 cracked open: headquarters, harbors, and key warehouses.

### Why it is different

- Current logic opportunistically prioritizes empty HQs or harbors.
- `Decapitation` would deliberately look for command/logistics collapse once a
  breach exists, even when the target is not trivially empty.

### Suggested score

```
score =
  6.0 * isHeadquarters
  + 4.0 * isHarbor
  + 3.0 * isWarehouse
  + 2.0 * disconnectsEnemyIslandOrSupplyArea
  - 3.0 * defendersPresent
  - 2.0 * enemyMilitaryCoverage
```

### Implementation sketch

- Extend the prioritized-target path for non-`nobMilitary` buildings.
- Gate it behind breach conditions:
  - enough local superiority
  - nearby enemy military coverage below a threshold
- Prefer logistics targets that are lightly screened by military posts.
- Optionally require that the target sits behind a military post already marked
  as capturable this turn.

### Expected behavior

- Produces more decisive wins when the front is already unstable.
- Adds a deliberate “go for the throat” mode instead of only taking whichever
  fort is easiest.

## 6. Economic Choke

### Goal

Target buildings whose capture is likely to starve a frontier rather than
 immediately destroy the most structures.

### Why it is different

- `Biting` optimizes for buildings lost on capture.
- `Economic Choke` optimizes for downstream military weakness over time.

### Suggested score

```
score =
  4.0 * cutsHarborAccess
  + 4.0 * isolatesWarehouseSupport
  + 3.0 * reducesNearbyEnemyTroopDensity
  + 2.0 * blocksExpansionRoute
  - 2.0 * immediateCounterattackRisk
```

### Implementation sketch

- Approximate logistical value with local structure patterns:
  - nearby harbor or warehouse support
  - isolated cluster behind the target
  - sparse alternate enemy military coverage
- This can start as a cheap heuristic without a full supply simulation.
- Consider it a slower, positional alternative to `Biting`.

### Expected behavior

- Better in longer games where pure attrition is too slow but reckless attacks
  are too costly.
- Encourages the AI to weaken a region before trying to roll it up.

## Recommended Rollout Order

If only a few new modes should be implemented, these have the clearest value:

1. `Sector Focus`
   It adds behavior the current selector lacks entirely: continuity between
   attacks.
2. `Breakthrough`
   It should improve frontier collapse and give the AI more purposeful
   offensives.
3. `Overmatch`
   It is cheap to implement with existing signals and should be easy to tune.

`Retaliation`, `Decapitation`, and `Economic Choke` are valuable, but they
benefit more from adding short-term combat memory or a bit more structural
analysis around logistics.

## Configuration Ideas

Any new mode should probably expose a few YAML weights under `combat`, for
example:

```yaml
combat:
  targetSelection: Breakthrough
  targetWeights:
    enemyReservePenalty: 2.0
    sectorStickiness: 4.0
    followUpOpportunity: 5.0
    retaliationWindow: 2000
    minStrengthLead: 3.0
```

This keeps the selector extensible without hardcoding one personality for all
AI difficulties.
