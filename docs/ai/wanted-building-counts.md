# AI Wanted Building Counts

This note describes how the JH AI estimates the wanted count for each
building type before construction jobs are queued.

See also:

- [construction-mechanics.md](construction-mechanics.md) - build-job queues,
  construction reservations, and `AIConstruction::Wanted()`.
- [configuration.md](configuration.md) - the `buildPlanner` YAML block that
  fills `AIConfig::wantedParams`.
- `libs/s25main/ai/aijh/planning/BuildingPlanner.cpp`
- `libs/s25main/ai/aijh/planning/BuildingCalculator.cpp`
- `libs/s25main/ai/aijh/config/WeightParams.cpp`

## Meaning Of The Count

`BuildingPlanner` stores one `buildingsWanted[BuildingType]` value per
building type. This is a target total, not a number of jobs to create.

The current count for a type is:

```cpp
buildingNums.buildings[type] + buildingNums.buildingSites[type]
```

So completed buildings and active building sites both satisfy demand. The
number still missing is:

```cpp
buildingsWanted[type] - GetNumBuildings(type)
```

`AIConstruction::Wanted(type)` uses that difference and compares it with
`constructionorders[type]`, the number of orders already placed in the current
execution wave. A non-military build job is allowed only while current orders
are below the missing count.

Military buildings and `Storehouse` do not use this per-type missing-count
check. They call `BuildingPlanner::WantMoreMilitaryBlds()` instead.

## Update Flow

`BuildingPlanner` is initialized by:

1. refreshing `buildingNums` from `GamePlayer::GetBuildingRegister()`
2. initializing `buildingsWanted`
3. calculating the first wanted counts

During play:

- `BuildingPlanner::Update()` refreshes current building counts, updates the
  expansion-required flag, calculates board balance, and periodically refreshes
  cached wood and stone resource availability.
- `AIEconomyController::PlanNewBuildings()` calls
  `UpdateBuildingsWanted()` before testing which buildings should be queued.
- `AIEventHandler` also calls `UpdateBuildingsWanted()` after relevant world
  events, so emergency or newly available demand can be reflected before jobs
  are added.

## Startup Set

If the AI has no military buildings and no military building sites,
`UpdateBuildingsWanted()` uses `BuildCalculator::GetStartupSet()` and returns
early.

The startup set seeds the basic economy:

- `Forester = 1`
- `Sawmill = 1`
- `Woodcutter = 1`
- `Quarry = 1 + numMilitaryBlds / 3`
- `Fishery = 1 + numMilitaryBlds / 5`

Several later-chain buildings are assigned `-1` in this unsigned array before
the values are copied into `buildingsWanted`. That is the code as written; it
acts like a sentinel in intent, but because the storage type is unsigned the
stored value is implementation dependent after the signed conversion in
`setBuildingsWanted()`.

## General Weighted Calculation

For most non-special building types, `BuildingPlanner` delegates to:

```cpp
BuildCalculator::Calc(type)
```

The calculator uses `aijh.GetConfig().wantedParams[type]`. If the wanted
parameters for a building type are not enabled, the wanted count is `0`.

The normal calculation has four stages.

## Worker Cap

The calculator first estimates how many workers could staff this building:

```cpp
maxWorkers(aijh, type)
```

For a normal workplace this is:

```cpp
available tool count + available worker count
```

For building types without a specific workplace job, helpers are used instead.

That worker count becomes a hard cap:

```cpp
maxBld = workersAvailable
       + CALC::calcCount(workersAvailable, wantedParams.workersAdvance)
```

If the AI already has at least `maxBld` completed buildings plus sites, the
calculator returns `maxBld` immediately.

## Weighted Demand Sources

The raw wanted count starts at `0`. The calculator then sums enabled weights
from four source groups in `WantedParams`:

- `bldWeights`: current counts of other building types
- `goodWeights`: goods in the player's inventory
- `statsWeights`: current player statistic values
- `resourceWeights`: AI resource availability; currently only `AIResource::Wood`
  is mapped by `BuildCalculator::getAvailableResource()`

For each enabled `BuildParams` entry:

1. read the source value
2. skip it if the source value is below `params.min`
3. calculate a contribution with `CALC::calcCount(sourceValue, params)`
4. clamp that contribution to `params.max`
5. add it to the raw wanted count

The raw count is clamped to at least `0` before productivity and final caps are
applied.

`CALC::calcCount()` combines the configured terms:

```cpp
constant
+ linear * x
+ exponential * exp(min(x, 50))
+ sign(logTwo.linear) * max(0, log(logTwo.constant + abs(logTwo.linear) * x))
```

This lets configuration express positive demand, negative stockpile pressure,
growth from map size or country statistic, and logarithmic scaling.

## Productivity Throttle

If the calculated count is above the current building-plus-site count, the
calculator may throttle growth by productivity:

- If productivity parameters are disabled, nothing happens.
- If at least one completed building exists and current productivity is below
  `productivity.min`, the calculator returns the current count. This prevents
  adding another copy while the existing chain is performing too poorly.
- If more than two completed buildings exist and productivity is below
  `productivity.max`, the calculator subtracts a sine-shaped malus from the
  raw count.

The current implementation sets the malus multiplier from
`count > currentBld`, so it is either `0` or `1`; it is not the numeric
difference between the calculated target and the current count.

## Final Caps

After productivity adjustment, the result is truncated to `unsigned` and
limited by both caps:

```cpp
min(result, maxBld, wantedParams.max)
```

`wantedParams.max` is the explicit per-building maximum from configuration.
`maxBld` is the worker-derived cap.

## BuildingPlanner Special Cases

`BuildingPlanner::UpdateBuildingsWanted()` wraps the general calculator with
several direct rules.

`Fishery` and `Hunter` are set before the normal loop:

```cpp
Fishery = min(maxFishers(aijh) + 1, numMilitaryBlds + 1)
Hunter  = min(maxHunters(aijh), 4)
```

`HarborBuilding` and `Shipyard` are initialized once in
`InitBuildingsWanted()` when relevant sea IDs exist:

- `HarborBuilding = 99`
- `Shipyard = 1` when there is one relevant sea ID
- `Shipyard = 99` when there are multiple relevant sea IDs

The normal calculator loop skips these types:

- `Fishery`
- `Hunter`
- `HarborBuilding`
- `Shipyard`
- `Catapult`
- `GraniteMine`
- `Charburner`

`Catapult` has a dedicated rule in the loop body, but it is currently also in
the skip list, so that code is unreachable as written.

After the normal loop, miner/tool pressure can override mine and metal-chain
targets. If available pickaxes plus miners are below `3`, the AI switches to an
emergency program:

```cpp
CoalMine     = 1
IronMine     = 1
GoldMine     = 0
Ironsmelter  = 1
Metalworks   = 1
Armory       = 0
GraniteMine  = 0
Mint         = 0
```

If the AI has at least three pickaxes plus miners, `IronMine` and `CoalMine`
are recalculated normally.

Gold can be disabled globally. When `IsGoldEnabled()` is false, `GoldMine` is
forced to `0` because coins cannot improve soldiers under those settings.

## Expansion And Military Demand

Military building demand is separate from `buildingsWanted`.

`BuildingPlanner::CalcIsExpansionRequired()` asks whether the AI lacks space
for the basic wood and stone chain around existing military buildings and
storehouses. If expansion is required, military construction is allowed even
without the usual economy signals.

`WantMoreMilitaryBlds()` blocks more military construction when active military
sites plus pending military orders reach:

```cpp
min(8, GetNumMilitaryBlds() + pendingMilitarySites + 3)
```

If the cap is not hit, it wants more military buildings when any of these are
true:

- expansion is required
- at least one sawmill exists
- boards are above `30` and a sawmill site exists
- the AI has fewer than three military buildings plus sites

## Practical Reading

For a normal economic building, the wanted count is best read as:

```text
configured weighted demand
limited by available workers and configured max
reduced or frozen by low productivity
minus completed buildings and active sites
minus jobs already ordered in this execution wave
```

For special buildings, check `BuildingPlanner::UpdateBuildingsWanted()` first,
because direct rules can replace or bypass the weighted calculator.
