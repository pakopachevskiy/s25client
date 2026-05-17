# Preferred AI Expansion Direction

## Problem

The current global position finder can score resources for normal economic
buildings, but military expansion mostly asks for any valid borderland plot.
In `tmp/expansion/version_CAAAB.yaml`, military buildings only require:

```yaml
Barracks:
  resources:
    Borderland: 1
```

The result is that a new military building can be placed on a legal border
tile without much understanding of which direction is strategically useful.
The AI should prefer expansion that secures resources it needs now, resources
it is likely to need soon, and neutral territory that would be costly to take
from an enemy later.

Relevant current code and docs:

- `docs/ai/position-finding.md`
- `docs/ai/wanted-building-counts.md`
- `libs/s25main/ai/aijh/planning/GlobalPositionFinder.cpp`
- `libs/s25main/ai/aijh/planning/BuildingPlanner.cpp`
- `libs/s25main/ai/aijh/planning/BuildingCalculator.cpp`
- `tmp/expansion/version_CAAAB.yaml`

## Goal

Choose military expansion plots by strategic value, not only by raw
`Borderland` value. The preferred direction should answer:

- Which resources are most needed now?
- Which resources will be needed in the mid-term economy?
- Which neutral resources should be claimed before an enemy reaches them?
- Which enemy-facing expansions are too expensive to capture and hold?
- Which positions are too hard to connect or defend?

## Proposed Score

Keep the existing `GlobalPositionFinder` filters. Only replace the rating used
for military building candidates.

```text
expansionScore =
  resourceGainScore
+ midTermResourceScore
+ defensiveClaimScore
- enemyContestCost
- logisticsCost
- overextensionCost
```

The score should be computed for every legal military candidate and the
highest positive value should win.

## Resource Demand Model

Build a live demand vector:

```cpp
helpers::EnumArray<double, AIResource> demand;
```

The demand vector should combine current shortages and mid-term planner intent.

Immediate demand examples:

- low coal stock or high coal consumption: boost `Coal`
- low iron ore or iron chain starvation: boost `Ironore`
- low stones or high board/stone construction demand: boost `Stones` and
  `Granite`
- low wood or boards bottleneck: boost `Wood`
- lack of farmable economy space: boost `Plantspace`

Mid-term demand examples:

- `CoalMine`, `IronMine`, `GoldMine`, or `GraniteMine` wanted: boost the
  matching underground resource.
- `Sawmill`, `Woodcutter`, or `Forester` wanted: boost `Wood` and
  `Plantspace`.
- `Farm`, `PigFarm`, `DonkeyBreeder`, `Bakery`, or `Brewery` wanted: boost
  `Plantspace` and the food-chain support resources.
- `Armory`, `Ironsmelter`, `Metalworks`, or strong military growth wanted:
  boost `Coal`, `Ironore`, and possibly `Gold`.
- gold disabled by game settings: reduce or zero `Gold` demand.

The existing `BuildingPlanner::GetNumAdditionalBuildingsWanted(type)` is a
good first source for mid-term demand. It already subtracts completed
buildings and active sites from the planner target.

## Candidate Territory Gain

For each military candidate, estimate which nearby tiles would become newly
owned or strategically secured if the building completed.

A first implementation can approximate this with the military radius instead
of simulating exact ownership:

```text
for each tile inside candidateMilitaryRadius:
  if tile is own inland territory:
    ignore or score very low
  if tile is own border territory:
    score as secured frontier
  if tile is neutral:
    score as cheap territory gain
  if tile is enemy-owned:
    score as contested territory gain
```

Exact ownership simulation can be added later if needed. The approximate
radius model is cheaper and should already steer expansion in a better
direction.

## Resource Gain Score

Score resources in the candidate's expected gained area:

```text
resourceGainScore =
  sum(resourceValueInGainedArea[res] * demand[res] * configResourceWeight[res])
```

Use available `AIResource` values:

- `Gold`
- `Ironore`
- `Coal`
- `Granite`
- `Fish`
- `Wood`
- `Stones`
- `Plantspace`
- `Borderland`

The current `AIQueryService::CalcResourceValue(pt, res)` can provide useful
local values. A later optimization could cache expansion-area resource totals
separately.

## Defensive Claim Score

Some neutral territory should be more valuable because an enemy is close to
claiming it. Taking that land early is often cheaper than capturing and
holding it later.

```text
defensiveClaimScore =
  neutralResourceValue
* demand[resource]
* enemyProximityPressure
* anticipatoryClaimWeight
```

`enemyProximityPressure` should rise when enemy military buildings are near
the same resource pocket, but before the candidate mostly overlaps enemy-owned
territory.

This encourages the AI to claim neutral coal, iron, gold, stone, or farm space
at a contested border before it turns into an attack target.

## Enemy Contest Cost

Enemy-facing expansion should remain possible, but only when payoff beats
cost.

```text
enemyContestCost =
  enemyOwnedTileCount * enemyTerritoryPenalty
+ nearbyEnemyMilitaryPower * enemyMilitaryPenalty
+ projectedCaptureRisk * captureRiskPenalty
```

Nearby enemy military power can start simple:

- count enemy military buildings in a radius around the candidate
- weight stronger buildings higher than barracks
- weight closer buildings higher than distant buildings
- optionally include known troop counts or attack strength when available

The AI already has related combat estimates:

- `GameWorldBase::LookForMilitaryBuildings(...)`
- `AICombatController::ComputeEnemyFrontlineWeight()`
- capture-risk estimates on existing own military buildings

For new candidate plots, a local enemy-pressure approximation is probably
enough for the first version.

## Logistics Cost

A valuable expansion is less useful if it is far from the economy or difficult
to connect.

```text
logisticsCost =
  distanceToNearestStorehouse * storehouseDistancePenalty
+ approximateRoadDistance * roadDistancePenalty
+ roadBqPenaltyEstimate
```

The first version can use distance to the nearest storehouse or connected road
flag. A later version can reuse road-route scoring from the construction path
if performance remains acceptable.

## Overextension Cost

When the AI is militarily weak or already building many military sites, it
should prefer safe consolidation over long aggressive expansion.

```text
overextensionCost =
  lowTroopFulfillmentPenalty
+ activeMilitarySitePenalty
+ recentlyLostNearbyPenalty
```

Useful signals already exist or are nearby:

- current military building site count
- pending military construction orders
- soldier availability
- `AIMilitaryLogistics::ComputeFulfillmentLevel(...)`
- recently lost military building positions

Recent losses near a candidate should strongly penalize the area for a while.

## First-Version Formula

A practical first version can use this shape:

```text
score =
  borderlandValue * 1.0
+ neededResourceGain * 8.0
+ futureResourceGain * 4.0
+ neutralClaimNearEnemy * 3.0
- enemyOwnedGain * 5.0
- nearbyEnemyMilitaryPower * 6.0
- distanceToStorehouse * 0.4
- recentlyLostNearby * 20.0
```

These numbers are intentionally rough. They should become configurable once
the implementation is working and measurable.

## Integration Point

Add a military-specific branch in
`GlobalPositionFinder::GetPointRating(...)`:

```cpp
if(BuildingProperties::IsMilitary(type))
    return ComputeMilitaryExpansionRating(type, pt);
```

Keep the existing scan filters unchanged:

- ownership/reachability/farm-state checks
- building-quality checks
- harbor proximity
- `Borderland` minimum requirement
- military flag-road rejection
- own military building proximity rejection

The new scorer should only decide which already-valid military candidate is
best.

## Configuration Shape

Static config should provide multipliers. Runtime code should compute the live
demand.

Example:

```yaml
posFinder:
  Barracks:
    expansion:
      resources:
        Coal: 8
        Ironore: 8
        Gold: 4
        Granite: 5
        Stones: 4
        Wood: 3
        Plantspace: 2
      futureDemandMultiplier: 4
      immediateDemandMultiplier: 8
      neutralClaimNearEnemy: 3
      enemyTerritoryPenalty: 5
      enemyMilitaryPenalty: 6
      storehouseDistancePenalty: 0.4
      recentlyLostPenalty: 20
```

The same expansion block can be inherited by all military building types, or
defined once in a global AI config section to avoid repeating it for
`Barracks`, `Guardhouse`, `Watchtower`, and `Fortress`.

## Expected Behavior

With this algorithm:

- the AI expands toward coal and iron when the metal chain is becoming the
  bottleneck
- the AI expands toward plant space when food and farms are limiting growth
- the AI prioritizes neutral high-value resource pockets near enemies before
  they become enemy territory
- the AI still attacks or pressures enemy territory when the payoff is high
  enough
- weak or overextended AI players avoid expensive contested expansion and
  prefer safer claims

This keeps expansion opportunistic, but gives the global military position
finder a strategic direction tied to the current and near-future economy.
