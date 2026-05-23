# Building Quality

Building quality is the per-plot answer to "what can be placed here?"  The
public value is `BuildingQuality` from
`libs/s25main/gameTypes/BuildingQuality.h`:

- `Nothing`: no flag or building can be placed.
- `Flag`: only a flag can be placed.
- `Mine`: a mine spot.
- `Hut`: a small building spot.
- `House`: a medium building spot.
- `Castle`: a large building spot.
- `Harbor`: a large harbor-building spot.

The enum order is intentional.  `canUseBq(found, required)` allows ordinary
buildings when the found quality is at least the required quality, but mines are
special: a mine can only be built on `BuildingQuality::Mine`.  Harbor spots rank
above `Castle`, so they can host ordinary large buildings as well as harbor
buildings.

## Raw Node Quality


The calculator treats the selected map point as the building position.  A
normal building's front flag is on the `SouthEast` neighbour.  Large
castle-sized buildings also need extension nodes on `West`, `NorthWest`, and
`NorthEast`.

## Calculation Steps

### 1. Blocking object on the selected point

If the selected point has any object whose `BlockingManner` is not `None`, the
result is immediately `Nothing`.  This catches existing flags, buildings,
building sites, trees, static objects, extensions, and other blocking objects.

### 2. Terrain around the point

The world checks all six terrain triangles around the point:

- any `TerrainBQ::Danger` terrain makes the result `Nothing`,
- six `TerrainBQ::Mine` triangles produce `Mine`,
- six `TerrainBQ::Castle` triangles produce `Castle`,
- any mixed non-empty buildable, mineable, or walkable terrain produces only
  `Flag`,
- all `TerrainBQ::Nothing` terrain produces `Nothing`.

`TerrainBQ` itself comes from `TerrainDesc::GetBQ()`: buildable terrain maps to
`Castle`, mineable terrain to `Mine`, walkable terrain to `Flag`, unreachable
terrain to `Danger`, and everything else to `Nothing`.

### 3. Altitude restrictions

Altitude can reduce a terrain-derived building spot:

- for a `Castle` spot, if the `SouthEast` flag point is more than one altitude
  level higher than the building point, the result becomes `Flag`,
- for a `Castle` spot, if any direct neighbour differs by more than three
  altitude levels, the result becomes `Flag`,
- for a remaining `Castle` spot, if any radius-2 neighbour differs by more than
  two altitude levels, the result becomes `Hut`,
- for a `Mine` spot, if the `SouthEast` flag point is more than three altitude
  levels higher, the result becomes `Flag`.

### 4. Nearby blocking manners

The calculator then looks at objects on the six direct neighbours:

- any `NothingAround` neighbour makes the result `Nothing`;
- a flag on `East` or `SouthWest` makes a non-flag building position
  `Nothing`;
- any `FlagsAround` neighbour reduces the result to `Flag`;
- nearby trees reduce qualities above `Hut` down to `Hut`;
- a `Castle` spot becomes `House` if any large-building extension point
  (`West`, `NorthWest`, `NorthEast`) is blocked;
- a `Castle` spot becomes `House` if a radius-2 point contains a building;
- a `Castle` spot becomes `House` if one of its extension points is on a road.

If the selected point itself is on a road, any non-flag result becomes `Flag`.
If the current result is `Flag`, an adjacent flag makes it `Nothing`;
otherwise the final result for this branch is `Flag`.

### 5. Harbor upgrade

If the result is still `Castle` and the node has a harbor id
(`MapNode::harborId`), the result becomes `Harbor`.  This means harbor quality
is not independent of the normal large-building checks: a harbor point must
still be a valid castle-sized plot first.

### 6. Front flag validation

For `Mine`, `Hut`, `House`, `Castle`, and `Harbor`, the building also needs a
front flag on the `SouthEast` neighbour.

The plot is accepted if that neighbour already contains a flag.  Otherwise the
calculator recursively checks the `SouthEast` point in `flagOnly` mode.  In this
mode, terrain only has to allow some non-empty non-danger quality, the point
must not be blocked, there must be no `NothingAround` neighbour, and no adjacent
flag may exist.

If the front flag can be placed, the building quality is kept.  If the front
flag cannot be placed, the selected point is downgraded to `Flag` when possible;
if an adjacent flag also blocks that fallback flag, the result becomes
`Nothing`.

## Player-Facing Quality

`World::GetBQ(pt, player)` applies ownership through `World::AdjustBQ()` before
the UI, commands, or AI use the value.

First, the selected point must be fully inside that player's territory:
`IsPlayerTerritory(pt, player + 1)` requires the point and all six neighbours to
have the same owner.  If not, the player-facing quality is `Nothing`.

Second, a non-flag building quality also requires the `SouthEast` front-flag
point to be fully inside player territory.  If the front-flag point is on the
border or outside territory, the player-facing result is reduced to `Flag`.
There is one extra guard: flags on `West`, `NorthWest`, or `NorthEast` can make
even that fallback flag impossible, in which case the result is `Nothing`.

`GameWorldViewer::GetBQ()` does the same adjustment for the local player's
visual map.  Its raw value may include a temporary road overlay while the player
is dragging a road, so preview roads can temporarily lower nearby displayed
qualities before the actual road command is committed.

## Recalculation Triggers

Raw building quality is cached per node, so callers must recalculate affected
nodes after changes:

- `InitAfterLoad()` recalculates the whole map.
- `RecalcBQAroundPoint()` recalculates the point and the first neighbour ring.
- `RecalcBQAroundPointBig()` also recalculates the radius-2 ring; building,
  large object, altitude, and many terrain-object changes use this.
- `RecalcBQForRoad()` recalculates the road point plus `East`, `SouthEast`, and
  `SouthWest`, which are the nearby plots affected by whether that point is on a
  road.
- Territory changes recalculate affected points after ownership changes,
  object destruction, and road removal.

Typical triggers include placing or destroying flags, buildings, building
sites, roads, large static objects, trees, fields, fires, charburner piles,
ship-founded harbor sites, and altitude changes.

## Placement Use

`GameWorld::SetBuildingSite()` validates a building by comparing the
player-facing `GetBQ(pt, player)` with that building's required size from
`BUILDING_SIZE` in `libs/s25main/gameData/BuildingConsts.h`.  The same
compatibility rule is used by AI placement searches.

`GameWorld::SetFlag()` only requires player-facing quality above `Nothing` and
then rejects the placement if another flag is adjacent.  Road building uses the
same idea for an automatically created endpoint flag.

The action UI reads `GameWorldViewer::GetBQ()` to decide which construction tab
to show: `Flag` only enables flag placement, while `Mine`, `Hut`, `House`,
`Castle`, and `Harbor` open the corresponding build tab.

## Important Consequences

- A raw `MapNode::bq` of `Castle` can still be `Nothing` for a player when the
  point is on the border or outside fully owned territory.
- Roads usually do not remove all construction value: a road point becomes
  `Flag`, while nearby castle plots often drop to `House` because large-building
  extensions cannot overlap roads.
- Large-building placement is stricter than medium placement because the
  extension points must be clear.
- Harbor quality is an upgrade of an otherwise valid `Castle` plot, not a
  separate terrain class.
- A building-quality result includes the ability to place or reuse the
  `SouthEast` front flag; it is not only a test of the building footprint.
