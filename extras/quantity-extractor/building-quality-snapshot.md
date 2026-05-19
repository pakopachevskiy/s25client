# BuildingQualitySnapshot Usage


`nodes.size()` should equal `map_width * map_height`.

## Enum Values

The snapshot stores engine enum ordinals as unsigned integers.

`TerrainBQ`:

- `0`: `Nothing`
- `1`: `Danger`
- `2`: `Flag`
- `3`: `Castle`
- `4`: `Mine`

`BuildingQuality`:

- `0`: `Nothing`
- `1`: `Flag`
- `2`: `Mine`
- `3`: `Hut`
- `4`: `House`
- `5`: `Castle`
- `6`: `Harbor`

`PointRoad`:

- `0`: `None`
- `1`: `Normal`
- `2`: `Donkey`
- `3`: `Boat`

`owner_id` follows the engine map-node convention: `0` is neutral and `1..N`
are player slots plus one. `adjusted_bq_by_player` is indexed by zero-based
engine player slot.

## Required Building Quality

The engine maps each `BuildingType` value to a required `BuildingQuality`.
Offline consumers can compare a player's `adjusted_bq_by_player[player_id]`
against this requirement to decide whether a building type can be placed on a
plot.  Mine buildings require `Mine` specifically.  `Nothing*` entries are
unused placeholder building types.

| BuildingType ordinal | BuildingType | Required BuildingQuality |
| ---: | --- | --- |
| 0 | `Headquarters` | `Castle` |
| 1 | `Barracks` | `Hut` |
| 2 | `Guardhouse` | `Hut` |
| 3 | `Nothing2` | `Nothing` |
| 4 | `Watchtower` | `House` |
| 5 | `Vineyard` | `Castle` |
| 6 | `Winery` | `House` |
| 7 | `Temple` | `Castle` |
| 8 | `Nothing6` | `Nothing` |
| 9 | `Fortress` | `Castle` |
| 10 | `GraniteMine` | `Mine` |
| 11 | `CoalMine` | `Mine` |
| 12 | `IronMine` | `Mine` |
| 13 | `GoldMine` | `Mine` |
| 14 | `LookoutTower` | `Hut` |
| 15 | `Nothing7` | `Nothing` |
| 16 | `Catapult` | `House` |
| 17 | `Woodcutter` | `Hut` |
| 18 | `Fishery` | `Hut` |
| 19 | `Quarry` | `Hut` |
| 20 | `Forester` | `Hut` |
| 21 | `Slaughterhouse` | `House` |
| 22 | `Hunter` | `Hut` |
| 23 | `Brewery` | `House` |
| 24 | `Armory` | `House` |
| 25 | `Metalworks` | `House` |
| 26 | `Ironsmelter` | `House` |
| 27 | `Charburner` | `Castle` |
| 28 | `PigFarm` | `Castle` |
| 29 | `Storehouse` | `House` |
| 30 | `Nothing9` | `Nothing` |
| 31 | `Mill` | `House` |
| 32 | `Bakery` | `House` |
| 33 | `Sawmill` | `House` |
| 34 | `Mint` | `House` |
| 35 | `Well` | `Hut` |
| 36 | `Shipyard` | `House` |
| 37 | `Farm` | `Castle` |
| 38 | `DonkeyBreeder` | `Castle` |
| 39 | `HarborBuilding` | `Harbor` |

## Reconstructing Roads

Each node stores only the engine's three canonical point-road directions:

- `road_east`
- `road_south_east`
- `road_south_west`

To answer whether a point is on any road, check those fields on the point and
the opposite directions stored by neighboring points:

- this point: `East`, `SouthEast`, `SouthWest`
- east neighbor stores the opposite of `West` as its `road_east`
- south-east neighbor stores the opposite of `NorthWest` as its
  `road_south_east`
- south-west neighbor stores the opposite of `NorthEast` as its
  `road_south_west`

Use the same wrapped hex-neighbor convention as `MapBase`.

## Raw Plot Quality

`raw_bq` is the engine's cached `MapNode::bq` after `InitAfterLoad()`. For most
offline consumers, this is the easiest source of plot quality before ownership
adjustment.

If you need to recalculate raw quality after a hypothetical object or road
change, mirror `BQCalculator`:

1. Return `Nothing` if the selected node has `blocking_manner != None`.
2. Inspect the six terrain triangles around the point. These are reconstructed
   from each node's `terrain_bq_1` and `terrain_bq_2` using the wrapped hex map
   geometry.
3. Start from terrain quality:
   - any `Danger` triangle -> `Nothing`;
   - six `Mine` triangles -> `Mine`;
   - six `Castle` triangles -> `Castle`;
   - any non-empty mixed quality -> `Flag`;
   - otherwise `Nothing`.
4. Apply altitude reductions:
   - a `Castle` plot becomes `Flag` if its `SouthEast` flag point is more than
     one altitude level higher;
   - a `Castle` plot becomes `Flag` if any direct neighbor differs by more than
     three altitude levels;
   - a remaining `Castle` plot becomes `Hut` if any radius-2 neighbor differs
     by more than two altitude levels;
   - a `Mine` plot becomes `Flag` if its `SouthEast` flag point is more than
     three altitude levels higher.
5. Apply neighboring blockers:
   - any `NothingAround` neighbor -> `Nothing`;
   - a flag on `East` or `SouthWest` blocks non-flag buildings;
   - any `FlagsAround` neighbor reduces to `Flag`;
   - nearby trees reduce qualities above `Hut` to `Hut`;
   - blocked large-building extension points (`West`, `NorthWest`,
     `NorthEast`) reduce `Castle` to `House`;
   - a building within radius 2 reduces `Castle` to `House`;
   - roads on large-building extension points reduce `Castle` to `House`.
6. If the selected point is on a road, reduce any non-flag quality to `Flag`.
7. If the result is `Flag`, any adjacent flag makes it `Nothing`.
8. If the result is still `Castle` and `harbor_id != 0`, upgrade it to
   `Harbor`.
9. For `Mine`, `Hut`, `House`, `Castle`, or `Harbor`, validate the front flag
   on the `SouthEast` neighbor. An existing flag is valid. Otherwise,
   recursively check whether a flag can be placed there. If not, downgrade the
   selected point to `Flag` when possible, or `Nothing` when adjacent flags also
   block that fallback.

## Player-Facing Quality

To calculate the quality for a specific zero-based player slot, apply ownership
to the raw quality. The snapshot already exports this in
`adjusted_bq_by_player[player_id]`.

To recompute it yourself:

1. If raw quality is `Nothing`, return `Nothing`.
2. The selected point must be fully inside that player's territory: the point
   and all six direct neighbors must have `owner_id == player_id + 1`.
   Otherwise return `Nothing`.
3. If raw quality is not `Flag`, the `SouthEast` front-flag point must also be
   fully inside that player's territory.
4. If the front-flag point is not fully owned, return `Flag`, unless a flag on
   `West`, `NorthWest`, or `NorthEast` blocks fallback flag placement. In that
   case return `Nothing`.
5. Otherwise return the raw quality.

## Hypothetical Updates

For "what if this object/road was removed" workflows:

- clear or change the relevant node's `blocking_manner`,
  `blocking_object_id`, and `blocking_go_type`;
- clear or change affected `PointRoad` fields when removing roads;
- recalculate only the affected neighborhoods:
  - object changes usually need the point plus first and sometimes second
    neighbor rings;
  - road changes need the changed road point plus nearby plots affected by
    `East`, `SouthEast`, and `SouthWest` storage;
  - ownership changes need affected owned cells and their neighbors.

Compare recalculated results with `raw_bq` and `adjusted_bq_by_player` to
validate your implementation against the engine snapshot.
