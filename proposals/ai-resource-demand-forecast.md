# AI Resource Demand And Mid-Term Forecast

## Problem

Preferred expansion direction needs a live estimate of which map resources are
valuable to the AI right now and which resources are likely to become valuable
soon. Without that estimate, military expansion can only rank border plots by
static terrain/resource values and cannot tell whether coal, iron, wood, farm
space, fish, or stone should drive expansion.

This proposal describes how to measure current demand and forecast mid-term
demand for use by strategic military expansion scoring.

Related proposal:

- `proposals/preferred-expansion-direction.md`

Relevant docs and code:

- `docs/ai/wanted-building-counts.md`
- `docs/ai/position-finding.md`
- `libs/s25main/ai/aijh/planning/BuildingPlanner.cpp`
- `libs/s25main/ai/aijh/planning/BuildingCalculator.cpp`
- `libs/s25main/ai/aijh/planning/PlannerHelper.cpp`
- `libs/s25main/ai/AIResource.h`

## Two Demand Layers

The AI should distinguish:

- Good demand: whether the economy needs goods such as coal, iron ore, boards,
  fish, bread, beer, stones, or tools.
- Map resource demand: whether expansion should prefer `AIResource` values such
  as `Coal`, `Ironore`, `Gold`, `Granite`, `Fish`, `Wood`, `Stones`, or
  `Plantspace`.

Expansion scoring needs map resource demand. Good demand and building demand
must therefore be converted into `AIResource` demand.

## Current Demand

Current demand answers: "What resource would relieve a present bottleneck?"

A practical current-demand score can combine:

```text
currentDemand[good] =
  stockShortage
+ activeConsumerPressure
+ constructionPipelinePressure
+ lowProductivityPressure
```

Useful existing signals:

- inventory goods from `GamePlayer::GetInventory().goods`
- active buildings and building sites from `BuildingPlanner`
- missing buildings from `BuildingPlanner::GetNumAdditionalBuildingsWanted`
- productivity from `AIWorldView::GetProductivity(type)`
- construction material demand from `BuildingRegister::CalcBoardsDemand()`
- expansion state from `BuildingPlanner::IsExpansionRequired()`

## Stock Shortage

For each important good, define a target stock and compare it with current
inventory:

```text
stockShortage(good) =
  clamp((targetStock(good) - inventory(good)) / targetStock(good), 0, 1)
```

Initial target stocks can be simple constants:

- boards: enough to keep construction moving
- stones: enough for military and medium buildings
- coal: enough for smelters, armories, and mints
- iron ore: enough for smelters
- iron: enough for tools and weapons
- fish/meat/bread: enough to feed mines
- water/grain/flour: enough to keep the food chain stable

These constants should later become config values.

## Active Consumer Pressure

A stockpile can be low because consumers are active. That should produce a
stronger signal than a low stockpile with no consumers.

Examples:

- active `Ironsmelter` plus low `Coal` or `IronOre` increases demand for
  `AIResource::Coal` and `AIResource::Ironore`
- active `Armory` plus low `Coal` or `Iron` increases demand for coal and the
  iron chain
- active `Mint` plus low `Coal` or `Gold` increases demand for coal and gold
  when gold is useful under game settings
- active mines plus low mine food increases demand for food-chain support

This pressure can start as:

```text
activeConsumerPressure(good) =
  activeConsumerCount(good) * stockShortage(good)
```

If productivity is available for the consumer building, low productivity can
multiply the pressure.

## Construction Pipeline Pressure

Construction demand is especially important for expansion because it can block
the AI before production chains recover.

Examples:

- high board demand and low boards: boost `Wood` and `Plantspace`
- low stones with military expansion active: boost `Stones` and `Granite`
- many building sites waiting for materials: boost the resources behind those
  materials

The current code already calculates board balance in `BuildingPlanner`:

```cpp
boardsBalance = inventory.goods[GoodType::Boards] - CalcBoardsDemand()
```

A negative board balance should become immediate demand for `Wood`, and if
foresters/farms compete for space it should also create some `Plantspace`
demand.

## Low Productivity Pressure

Low productivity means the AI has buildings that exist but are not satisfying
the economy.

Examples:

- low sawmill productivity: wood supply or road logistics may be bad; boost
  `Wood` moderately
- low mine productivity: mine food may be lacking; boost food-chain support
- low farm-chain productivity: boost `Plantspace`
- low quarry/granite productivity: boost `Stones` or `Granite`

This should be a secondary signal. It is useful for direction choice, but it
should not dominate direct stock shortages or wanted producer buildings.

## Mid-Term Forecast

Mid-term demand answers: "What resources will be needed by the buildings the
AI is already planning to build?"

The first forecast should use `BuildingPlanner` rather than a full economy
simulation:

```text
futureBuildingNeed[type] =
  max(0, buildingsWanted[type] - currentBuildingsAndSites[type])
```

In code this is already exposed as:

```cpp
BuildingPlanner::GetNumAdditionalBuildingsWanted(type)
```

Positive values represent the planner's near-future intent after accounting
for completed buildings and active sites.

## Convert Future Buildings To Map Resources

Wanted producer buildings map directly to expansion resources:

```text
CoalMine      -> AIResource::Coal
IronMine      -> AIResource::Ironore
GoldMine      -> AIResource::Gold
GraniteMine   -> AIResource::Granite
Quarry        -> AIResource::Stones
Woodcutter    -> AIResource::Wood
Forester      -> AIResource::Plantspace
Farm          -> AIResource::Plantspace
Fishery       -> AIResource::Fish
```

Wanted consumer buildings create downstream resource pressure:

```text
Ironsmelter   -> Coal + Ironore
Armory        -> Coal + Ironore + Plantspace/food support
Metalworks    -> Ironore + Coal + Wood
Mint          -> Gold + Coal
Bakery        -> Plantspace
Brewery       -> Plantspace
PigFarm       -> Plantspace
DonkeyBreeder -> Plantspace
```

Wanted mines also create food demand because mines consume fish, meat, or
bread:

```text
CoalMine/IronMine/GoldMine/GraniteMine -> Fish + Plantspace food chain
```

## Feasibility Cap

Demand should be capped by the AI's ability to exploit the resource soon.

Examples:

- If the AI has no miners or pickaxes, mine-resource demand is real but should
  be capped until the emergency metal/tool chain is underway.
- If gold is disabled by game settings, `Gold` demand should be zero or near
  zero.
- If there are no fishery tools or fishable water is scarce, `Fish` demand
  should not dominate.
- If there are no sawmill or woodcutter tools, wood demand should remain
  present but not consume all expansion priority.

This can be expressed as:

```text
effectiveDemand[resource] =
  rawDemand[resource] * exploitabilityFactor[resource]
```

`exploitabilityFactor` can start with simple values based on workers, tools,
and whether the producer building is enabled.

## Overstocks And Negative Demand

Large stockpiles should reduce demand for the matching resource.

Examples:

- high coal stock lowers `Coal` demand
- high gold stock lowers `Gold` demand
- high stones stock lowers `Stones`/`Granite` demand
- high boards plus stable wood chain lowers `Wood` demand

The existing build planner config already uses this style with negative
weights, for example stockpile penalties in `buildPlanner` good weights. The
resource demand model should use the same idea:

```text
overstockPenalty(good) =
  clamp((inventory(good) - highStock(good)) / highStock(good), 0, maxPenalty)
```

## Combined Demand Vector

For expansion scoring, combine immediate and mid-term demand:

```text
resourceDemand[res] =
  immediateWeight * currentDemand[res]
+ futureWeight * midTermDemand[res]
+ strategicFloor[res]
- overstockPenalty[res]
```

Suggested initial weights:

```text
immediateWeight = 8
futureWeight    = 4
strategicFloor  = small value for coal, iron, stone, wood, and plant space
```

The strategic floor prevents the AI from treating generally valuable resources
as worthless just because the current inventory is temporarily comfortable.

## Smoothing

Demand should not jump sharply every planning tick. Smooth the vector over
time:

```text
smoothedDemand[res] =
  0.7 * previousDemand[res]
+ 0.3 * newlyCalculatedDemand[res]
```

This avoids expansion direction thrashing when inventory crosses a threshold
or one building site completes.

## First Implementation Scope

A useful first implementation can be small:

1. Create `AIResourceDemandEstimator`.
2. Read inventory, building counts, `GetNumAdditionalBuildingsWanted`, and
   productivity.
3. Produce `helpers::EnumArray<double, AIResource>`.
4. Convert obvious building needs into resource demand.
5. Apply simple exploitability caps.
6. Smooth the result per AI player.
7. Feed the vector into military expansion scoring.

The first version does not need a full production simulation. It only needs to
rank expansion directions better than static borderland scoring.

## Practical Formula

For each map resource:

```text
resourceDemand =
  shortageScore
+ wantedProducerScore
+ downstreamConsumerScore
- overstockPenalty
```

Then:

```text
resourceDemand =
  resourceDemand * exploitabilityFactor
```

And finally:

```text
smoothedDemand =
  0.7 * previousDemand + 0.3 * resourceDemand
```

This gives the military position scorer a live, economy-aware vector: expand
toward what the AI is short on now, what its planner is about to build, and
what its production chains will soon consume.
