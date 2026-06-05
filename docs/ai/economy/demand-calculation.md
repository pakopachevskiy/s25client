# AI Ware Demand Calculation

This note describes how `WareDemandStatsHolder` builds the per-player demand
snapshot used by AI economy analysis.

See also:

- [../wares-distribution.md](../wares-distribution.md) - how concrete wares
  are routed to consumers after they exist.
- [../construction-mechanics.md](../construction-mechanics.md) - how AI build
  jobs become active construction sites.
- [../../gameplay/economy/construction-economy.md](../../gameplay/economy/construction-economy.md)
  - gameplay flow for boards, stones, builders, and sites.
- `libs/s25main/WareDemandStatsHolder.cpp`
- `libs/s25main/WareProductionStatsHolder.h`

## Snapshot Scope

`WareDemandStatsHolder::GetCurrentDemand()` returns a `WareDemandSnapshot`:

```cpp
struct WareDemandSnapshot
{
    helpers::EnumArray<uint32_t, GoodType> demand{};
    helpers::EnumArray<bool, GoodType> calculated{};
};
```

`demand[good]` is the estimated demand for one stats window. `calculated[good]`
marks goods whose demand model is supported, even when the current demand value
is `0`.

The stats window is shared with `WareProductionStatsHolder`:

```cpp
WareProductionStatsHolder::WINDOW_SIZE_GF == 5000
```

Snapshots are cached per player, world pointer, and 5000-GF window. A later
call in the same window returns the cached value even if buildings are enabled,
disabled, staffed, completed, or destroyed after the first calculation.
`WareDemandStatsHolder::Reset()` clears this cache.

Invalid player IDs and unused player slots return an empty snapshot with no
goods marked as calculated.

## Supported Demand Sources

The snapshot currently combines two demand sources:

- recurring input demand from staffed, enabled production buildings
- construction-material demand from current building sites

The public API still accepts an `AIPlayer*`, but the current calculation does
not use AI wanted building counts, build-job queues, or builder availability.
The world state owned by `GamePlayer` is the source of truth.

## Recurring Building Demand

Recurring demand is calculated from `BLD_WORK_DESC[type]` for usual buildings.
The following building types are skipped:

- `Temple`
- `Catapult`
- `Brewery`
- `Armory`
- `Mint`

A usual building type contributes only when all of these are true:

- `BLD_WORK_DESC[type].waresNeeded` is not empty
- the work description has a job
- the specific building has a worker
- production is not disabled for that building

The cycle length is:

```text
cycleGf = JOB_CONSTS[job].wait1_length
        + JOB_CONSTS[job].work_length
        + JOB_CONSTS[job].wait2_length
        + 40
```

The number of cycles demanded in one window is rounded up:

```text
cyclesPerWindow = ceil(5000 / cycleGf)
```

If `BldWorkDescription::useOneWareEach` is true, each ware in `waresNeeded`
receives `cyclesPerWindow` demand per active building. This models buildings
that consume every listed input during each work cycle.

If `useOneWareEach` is false, `cyclesPerWindow` is split across the accepted
wares in declaration order:

```text
baseDemand = cyclesPerWindow / numWareTypes
remainder  = cyclesPerWindow % numWareTypes
```

Each ware receives `baseDemand`, and the first `remainder` wares receive one
extra unit. This is used for interchangeable inputs such as mine food.

All goods are normalized with `ConvertShields()` before they are marked or
added. Shield variants therefore contribute to the normalized shield good.

## Construction Demand

Construction demand is calculated as the full material cost of all current
building sites as if every site had just started.

The calculation iterates:

```cpp
player.GetBuildingRegister().GetBuildingSites()
```

For each `noBuildingSite`, it adds the blueprint cost:

```text
boards += BUILDING_COSTS[site->GetBuildingType()].boards
stones += BUILDING_COSTS[site->GetBuildingType()].stones
```

Boards and stones are marked as calculated regardless of whether any building
sites currently exist.

This deliberately does not subtract materials that have already been delivered,
materials that have already been used by the builder, or materials currently on
the road. It also does not estimate future sites from AI planner deficits. The
number answers a different question:

```text
How many boards and stones would be needed to supply all active sites
if those sites had all been placed just now?
```

This makes construction demand a stable active-workload estimate instead of a
shortage or remaining-delivery estimate.

## What Is Not Included

The snapshot is not a complete economic forecast. It does not include:

- future build jobs that have not become building sites
- AI wanted building counts
- available builder or hammer limits
- already queued construction orders in the current AI execution wave
- expedition material demand from harbor buildings
- warehouse stock, road distance, or distribution-priority effects
- disabled or unstaffed production buildings
- actual consumption already observed in the current window

Actual production and consumption are tracked separately by
`WareProductionStatsHolder`. Demand is a model of expected need; production
stats are measurements of what happened.

## Example

Assume the current player has:

- one staffed, enabled sawmill
- one active woodcutter site
- one active farm site

The sawmill adds wood demand using its worker cycle:

```text
wood += ceil(5000 / sawmillCycleGf)
```

The construction sites add full costs:

```text
boards += BUILDING_COSTS[Woodcutter].boards
boards += BUILDING_COSTS[Farm].boards
stones += BUILDING_COSTS[Woodcutter].stones
stones += BUILDING_COSTS[Farm].stones
```

If one board has already arrived at the woodcutter site, the demand is
unchanged because construction demand is based on full site cost, not remaining
site shortage.
