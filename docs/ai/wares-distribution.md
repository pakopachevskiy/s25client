# AI Wares Distribution

This note describes how the current JH AI distributes wares among consumers.

The important distinction is:

- the shared economy code routes each concrete ware to a consumer
- the JH AI adjusts the same distribution priorities that a human player can
  change in the distribution window
- consumers can also pull existing stock from warehouses when their local
  buffers run low

The AI therefore does not directly choose a destination building for every
produced ware. It shapes the routing policy and lets `GamePlayer` perform the
actual destination selection.

See also:

- `docs/gameplay/economy/production-chains.md`
- `docs/ai/configuration.md`
- `docs/ai/gold-distribution.md`

## Overview

Ware flow has three main layers:

1. A worker produces a ware and places it at the workplace flag.
2. `GamePlayer::FindClientForWare(const Ware&)` scores eligible consumers and
   assigns the best reachable destination.
3. If no consumer qualifies, the ware is sent to an appropriate warehouse.

The relevant entry point for produced wares is
`nofBuildingWorker::WorkingReady()`. After creating the ware, it calls:

```cpp
real_ware->SetGoal(world->GetPlayer(player).FindClientForWare(*real_ware));
```

The ware then calculates its route and enters the road network.

Other sources of free wares, such as refunded construction materials or wares
explicitly exported from a warehouse, use the same destination-selection path.

## Distribution Priority Data

`GamePlayer::Distribution` stores the routing state for each ware:

```cpp
struct Distribution
{
    helpers::EnumArray<uint8_t, BuildingType> percent_buildings;
    std::vector<BuildingType> client_buildings;
    std::vector<BuildingType> goals;
    unsigned selected_goal;
};
```

Despite the internal `percent_buildings` name, the values are priority weights,
not strict percentages.

The configurable distribution edges are defined in
`libs/s25main/gameTypes/SettingsTypes.cpp`. They cover shared resources such as:

- food for mines and temples
- grain for mills, pig farms, donkey breeders, breweries, and charburners
- iron for armories and metalworks
- coal for armories, iron smelters, and mints
- wood for sawmills, charburners, and vineyards
- boards for construction sites, metalworks, and shipyards
- water for bakeries, breweries, pig farms, donkey breeders, and vineyards

Some wares have fixed consumer lists instead of configurable priorities. For
example, flour goes to bakeries, gold goes to mints, iron ore goes to iron
smelters, and ham goes to slaughterhouses.

Bread and meat reuse the food distribution settings stored under
`GoodType::Fish`.

## Weighted Consumer-Type Schedule

`GamePlayer::RecalcDistributionOfWare()` rebuilds a weighted schedule whenever
distribution settings change.

For example, if iron distribution is:

```text
Armory:     8
Metalworks: 4
```

the `goals` list contains eight `Armory` entries and four `Metalworks` entries.
After a ware is successfully assigned, the selected entry advances with:

```cpp
selected_goal = (selected_goal + 907) % goals.size();
```

When scoring ordinary consumer buildings:

- buildings of the currently selected type receive `+300` points
- buildings of other types lose `300` points
- a priority value of `0` removes that consumer type entirely

This creates a long-term weighted preference without overriding local demand
or road distance. The schedule does not guarantee an exact ratio: a selected
type may be full, disabled, disconnected, or sufficiently far away that a
different consumer still wins.

## Ordinary Consumer Score

For normal production buildings, `nobUsual::CalcDistributionPoints()` first
checks whether the ware is useful:

- disabled buildings return `0`
- buildings that do not consume the ware return `0`
- buildings whose local buffer is already full return `0`

Eligible buildings start with:

```text
points = 10000
points -= 30 * (wares_already_stored + wares_already_on_the_way)
```

`GamePlayer::FindClientForWare()` then applies the weighted consumer-type bonus
or penalty and accounts for route length:

```text
effective_score = adjusted_points - road_path_length / 2
```

The highest-scoring reachable building receives the ware.

The implementation uses straight-line distance as a cheap estimate before
performing road pathfinding. Candidates are sorted by that estimate, and
pathfinding is skipped once the remaining candidates cannot beat the best
confirmed score. This is an optimization; the final choice uses road-network
distance.

## Consumer Pull From Warehouses

Destination selection for newly created wares is only one half of the flow.
Consumer buildings also request existing warehouse stock.

`nobUsual` keeps local input buffers. When a buffer is low, it calls:

```cpp
world->GetPlayer(player).OrderWare(requiredWare, this);
```

`GamePlayer::OrderWare()` finds the nearest reachable warehouse with that ware
and asks it to dispatch one unit. If no warehouse can deliver, the method also
checks for matching lost wares waiting at flags and retargets the closest
reachable one.

This means consumer supply is a combination of:

- push routing for newly produced wares
- pull orders for wares already stored in warehouses
- recovery of stranded wares when a new consumer can use them

## Warehouse Fallback

If no consumer can receive a free ware, `GamePlayer::FindWarehouseForWare()`
chooses a warehouse destination.

The preference order is:

1. A warehouse configured to collect the ware.
2. A warehouse that accepts the ware and is not configured to send it out.
3. Any warehouse that accepts the ware.

The second step avoids immediately returning a ware to a warehouse that is
trying to export it.

Warehouse acceptance is controlled by per-ware inventory settings. A
warehouse with the `Stop` flag set for a ware does not accept that ware.

## Construction Materials

Construction sites are represented specially in the distribution table:
`BuildingType::Headquarters` means construction sites because headquarters
cannot be built normally.

`noBuildingSite::CalcDistributionPoints()` handles boards and stones. A site
returns `0` while it is still being leveled or when the requested material is
already fully supplied.

Eligible sites start with `10000` points and lose points for:

- materials still required before completion
- lower construction priority according to the player's build order

This favors high-priority sites that are closer to completion.

For boards, the configurable construction priority adds another:

```text
construction_slider_value * 30
```

to each site's score.

## Harbor Expeditions

Active harbor expeditions are additional consumers for boards and stones.

`nobHarborBuilding::CalcDistributionPoints()` returns `0` unless an expedition
is active and still needs the requested material. Eligible expeditions start
with `10000` points and gain points for materials already stored or on the way.

`GamePlayer::FindClientForWare()` adds another fixed `300` points, giving
expeditions a high priority even though they do not have a distribution
slider.

## Coins

Coins bypass the ordinary wares-distribution schedule.

`GamePlayer::FindClientForWare()` delegates them to
`GamePlayer::FindClientForCoin()`, which scores eligible military buildings
using:

```text
points = 10000
points -= 30 * (stored_coins + ordered_coins)
points += 20 * upgradeable_soldiers
effective_score = points - road_path_length
```

Coin distribution is documented in more detail in
`docs/ai/gold-distribution.md`.

## JH AI Initial Priorities

`AIEconomyController::InitDistribution()` installs JH AI-specific priorities
through the same `ChangeDistribution()` path used by the player-facing
distribution window.

The initial JH AI priorities are:

| Ware | Consumer priorities |
|------|---------------------|
| Food | Granite mine `10`, coal mine `10`, iron mine `10`, gold mine `10`, temple `2` |
| Grain | Mill `10`, pig farm `10`, donkey breeder `10`, brewery `10`, charburner `10` |
| Iron | Armory `10`, metalworks `10` |
| Coal | Armory `10`, iron smelter `10`, mint `10` |
| Wood | Sawmill `10`, charburner `10`, vineyard `2` |
| Boards | Construction `10`, metalworks `4`, shipyard `2` |
| Water | Bakery `10`, brewery `10`, pig farm `10`, donkey breeder `10`, vineyard `2` |

These replace the generic player defaults for AI-controlled players.

## Dynamic AI Adjustments

`AIPlayerJH::RunGF()` calls `AIEconomyController::AdjustSettings()` every 150
game frames, staggered by player ID. `AdjustSettings()` calls
`AdjustDistribution()`.

### Metalworks Iron

The AI reduces `Iron -> Metalworks` priority when every tool inventory meets
its baseline.

The reduction grows with the smallest tool surplus:

```text
decrease = 2 + min_tool_surplus * 2
new_priority = max(0, base_priority - decrease)
```

If any tool is below baseline, the priority returns to its initial AI value.
This preserves more iron for armories when tool stock is already healthy.

### Configurable Overstock Penalties

`AIConfig::distributionParams[GoodType][BuildingType]` can define penalties
for individual distribution edges. Each penalty observes the stock of another
ware and adjusts the slider value when that stock exceeds a configured
minimum.

The built-in defaults are:

- reduce `Grain -> Brewery` as beer stock grows
- reduce `Coal -> Mint` as coin stock grows

Additional edges can be configured through the YAML `distributionAdjuster`
section documented in `docs/ai/configuration.md`.

Adjusted values are clamped to the slider range `0..10`.

## Warehouse Stock Balancing

The AI also has a separate warehouse-balancing mechanism. It should not be
confused with consumer selection.

During building planning, `AIEconomyController::PlanNewBuildings()` calls:

```cpp
DistributeGoodsByBlocking(GoodType::Boards, 30);
DistributeGoodsByBlocking(GoodType::Stones, 50);
```

`DistributeGoodsByBlocking()` groups warehouses by road reachability and
toggles each warehouse's `Stop` inventory flag. Warehouses above the target
stock limit stop accepting the ware while warehouses at or below the limit
continue accepting it.

There are two escape conditions that clear blocking:

- if every warehouse in a reachable group is above the limit, all warehouses
  in that group accept the ware again
- if harbors make up at least half of the storehouses, all storehouses accept
  the ware

These conditions avoid leaving the economy without an accepting warehouse.

This spreads boards and stones among warehouses. It does not directly choose
which production building or construction site receives the next ware.

## Road workload debug overlay

The JH AI maintains a read-only road workload snapshot for diagnostics. It
estimates pressure on each owned flag-to-flag road segment by routing
predicted current ware dispatches with the normal ware-mode road pathfinder.
That includes carrier congestion penalties, land roads, donkey roads,
waterways, and road portions on either side of ship connections. Ship hops
themselves have no segment score.

The model contributes one point for each routed edge:

1. enabled producer to the destination `FindClientForWare()` would currently
   choose for its produced ware
2. stocked warehouse ware with `Send` enabled to the destination
   `FindClientForWare()` would currently choose
3. stocked warehouse to a consumer that can currently pull that ware

Producer and warehouse-export destination previews use local distribution
cursors, so the snapshot follows weighted distribution priorities without
advancing the player's real distribution state. Consumers include ordinary
buildings with positive distribution points, construction sites and active
expeditions missing boards or stones, and military buildings with positive
coin demand. Empty warehouses, disabled producers, full consumers,
disconnected pairs, and same-endpoint pairs do not contribute.

The snapshot initializes every owned registered road segment to `0`, then
expands each score onto the non-flag interior tiles of that segment. Endpoint
flags are intentionally omitted because a junction can belong to multiple
segments. The `Road Workload` AI debug overlay draws the cached numeric value,
including zero. Selecting the overlay does not recalculate it.

The AI refreshes the snapshot once after startup initialization and then in
the player-staggered `1500 GF` economic-maintenance cadence. The values are
diagnostic snapshots and can therefore remain stale for up to `1500 GF`;
global road-workload bypass decisions refresh and consume the segment list on
their own slower cadence.

## Route Failures And Lost Wares

If a route disappears while a ware is in transit, the ware notifies its
consumer and tries to find a warehouse instead.

`GamePlayer::FindClientForLostWares()` revisits stranded wares after relevant
network changes and attempts to route them into warehouses. Later, a consumer
order may claim a matching lost ware through `GamePlayer::OrderWare()`.

## Practical Summary

The current AI ware-distribution policy is:

1. Initialize AI-specific consumer-type priorities.
2. Periodically adjust selected priorities based on stock levels.
3. Let the shared economy score concrete consumer buildings by demand,
   weighted type preference, and transport distance.
4. Send wares without a suitable consumer to warehouses.
5. Let consumers pull warehouse stock when their local buffers run low.
6. Independently balance boards and stones among warehouses with inventory
   blocking.

The most relevant implementation files are:

- `libs/s25main/GamePlayer.cpp`
- `libs/s25main/gameTypes/SettingsTypes.cpp`
- `libs/s25main/buildings/nobUsual.cpp`
- `libs/s25main/buildings/noBuildingSite.cpp`
- `libs/s25main/buildings/nobHarborBuilding.cpp`
- `libs/s25main/buildings/nobBaseWarehouse.cpp`
- `libs/s25main/ai/aijh/runtime/AIEconomyController.cpp`
- `libs/s25main/ai/aijh/config/AIConfig.cpp`
