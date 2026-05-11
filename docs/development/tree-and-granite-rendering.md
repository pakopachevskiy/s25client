# Tree And Granite Rendering

This note summarizes how functional environment objects, specifically trees and
granite rocks, become visible on screen in the runtime renderer.

## Rendering Split

Trees and granite are normal nodal objects. They are not part of the terrain
mesh and they are not drawn by `TerrainRenderer`.

After terrain rendering, `GameWorldView` walks the visible map rows and draws
the object stored on each visible node:

- `GameWorldView::Draw(...)` handles visibility and row ordering in
  [libs/s25main/world/GameWorldView.cpp](../../libs/s25main/world/GameWorldView.cpp).
- `GameWorldView::DrawObject(...)` fetches `MapNode::obj` and calls
  `obj->Draw(curPos)`.
- `noTree::Draw(...)` and `noGranite::Draw(...)` choose the actual sprite from
  loader caches.

That means trees and granite are layered like other map objects and figures,
not like roads. Terrain shading is already baked into the terrain pass; tree
and granite sprites use their own bitmap shadows from the map graphics archive.

## Map Import

Initial trees and granite rocks are created while loading the map object layers
in [libs/s25main/world/MapLoader.cpp](../../libs/s25main/world/MapLoader.cpp).

`MapLoader::PlaceObjects(...)` reads the `ObjectType` / `ObjectIndex` pair from
the parsed `.WLD` / `.SWD` data:

- `ObjectType == 0xC4` imports tree types 0 through 3.
- `ObjectType == 0xC5` imports tree types 4 through 7.
- `ObjectType == 0xC6` imports tree type 8.
- `ObjectType == 0xCC` imports granite type `GraniteType::One`.
- `ObjectType == 0xCD` imports granite type `GraniteType::Two`.

Map-file trees are created as fully grown `noTree(pt, type, 3)` objects.

Granite object indices `0x01` through `0x07` are converted into a `noGranite`
state with `noGranite::EncodeBoostedState(lc - 1)`. The boosted state preserves
the legacy visual size but gives the rock twice the stonemason yield.

## Sprite Cache Construction

Tree and granite sprites are cached during `Loader::fillCaches()` in
[libs/s25main/Loader.cpp](../../libs/s25main/Loader.cpp).

Trees use a two-dimensional cache:

```cpp
tree_cache[type][ani_step]
```

There are 9 tree types and 15 frames per type. For each cached frame, the
loader combines:

- image `200 + type * 15 + ani_step`
- shadow image `350 + type * 15 + ani_step`

Granite uses a cache indexed by granite type and visual size:

```cpp
granite_cache[type][size]
```

There are 2 granite types and 6 visual sizes per type. For each cached size,
the loader combines:

- image `516 + graniteType * 6 + size`
- shadow image `616 + graniteType * 6 + size`

The cache entries are `glSmartBitmap` objects, so normal visible rendering can
draw sprite and shadow together with a single cache lookup.

## Tree Rendering

The tree object is implemented by
[libs/s25main/nodeObjs/noTree.cpp](../../libs/s25main/nodeObjs/noTree.cpp).

`noTree` stores:

- `type`: tree species, in range 0 through 8
- `size`: growth size, where 0 through 2 are young trees and 3 is fully grown
- `state`: standing, growing, waiting to fall, falling, or fallen
- scheduled growth and animal-production events

The draw frame depends on the state:

- Fully grown standing trees use animated frames 0 through 7. The selected
  frame comes from `GAMECLIENT.GetGlobalAnimation(...)`, with map coordinates
  mixed into the animation parameters so neighboring trees do not all move in
  lockstep.
- Young trees in `GrowingWait` draw frame `8 + size`.
- Trees in `GrowingGrow` fade from the current young frame into the next young
  frame, or into frame 0 when reaching full size.
- Falling trees draw frames 11 through 13.
- Fallen trees draw frame 14.

Every fully grown standing tree increments `noTree::DRAW_COUNTER` when drawn.
`dskGameInterface::Run()` resets the counter before the frame and passes the
final value to `SoundManager::playBirdSounds(...)`, so visible rendered trees
also influence ambient bird sounds.

## Granite Rendering

The granite object is implemented by
[libs/s25main/nodeObjs/noGranite.cpp](../../libs/s25main/nodeObjs/noGranite.cpp).

`noGranite` stores:

- `type`: one of the two granite sprite families
- `state`: a serialized durability value plus an optional boosted-state flag

`noGranite::Draw(...)` is intentionally simple:

```cpp
LOADER.granite_cache[type][GetVisualSize()].draw(drawPt);
```

`GetVisualSize()` masks off the boosted-state flag, converts boosted durability
back to the legacy visual scale, and clamps the result to cache indices 0
through 5.

Stonemasons reduce granite through
[libs/s25main/figures/nofStonemason.cpp](../../libs/s25main/figures/nofStonemason.cpp):

- if the rock is already at its smallest raw state, the object is destroyed
- otherwise `noGranite::Hew()` decrements the raw state

The normal world draw pass reflects the smaller size immediately because the
next frame reads the current `noGranite` state. The minimap is only notified
when the rock disappears, because the minimap color does not encode granite
size.

## Runtime Creation And Removal

Trees can appear after map load through foresters. When a forester finishes
work in
[libs/s25main/figures/nofForester.cpp](../../libs/s25main/figures/nofForester.cpp),
it may replace an empty or decorative environment node with a new young tree:

```cpp
world->SetNO(pos, new noTree(pos, RANDOM_ELEMENT(AVAILABLE_TREES[landscapeType]), 0));
```

The planted tree starts at size 0, schedules its growth events, recalculates
nearby building quality, and updates the minimap.

Trees are removed through the woodcutter path. `noTree::FallSoon()` schedules a
falling animation, and after the fallen-tree delay the object replaces itself
with a disappearing stump object, recalculates nearby building quality, and
updates the minimap.

Granite rocks are removed by stonemasons when the smallest rock is hewn. The
node object is destroyed, nearby building quality is recalculated, and the
minimap node is marked dirty.

## Fog Of War

Visible trees and granite are drawn from live world objects. Fogged trees and
granite are drawn from remembered `FOWObject` snapshots.

When a node moves into fog memory, `World::SaveFOWNode(...)` in
[libs/s25main/world/World.cpp](../../libs/s25main/world/World.cpp) calls
`GetNO(pt)->CreateFOWObject()`.

For trees and granite:

- `noTree::CreateFOWObject()` stores the tree type and size in `fowTree`.
- `noGranite::CreateFOWObject()` stores the granite type and state in
  `fowGranite`.

The fog draw path is implemented in
[libs/s25main/FOWObjects.cpp](../../libs/s25main/FOWObjects.cpp):

- `fowTree::Draw(...)` draws either the fully grown base frame or a young-tree
  frame, tinted with the fog color, plus the matching shadow.
- `fowGranite::Draw(...)` computes the same visual-size index as live granite
  and draws the cached granite bitmap with the fog color.

Fog rendering is intentionally static. It is a remembered visual state, not a
live animated object.

## Minimap Rendering

Tree and granite minimap coloring is covered in more detail by
[map-file-parsing-and-minimap-rendering.md](map-file-parsing-and-minimap-rendering.md).

The relevant rendering behavior is:

- map previews detect trees and granite directly from raw object layers
- ingame minimaps detect live `NodalObjectType::Tree` /
  `NodalObjectType::Granite`, or remembered `FoW_Type::Tree` /
  `FoW_Type::Granite`
- both object classes use dedicated minimap colors instead of terrain colors

The ingame minimap does not distinguish tree species, tree animation state,
granite type, or granite size. It only marks the node as tree or granite, with
optional territory-color blending.

## Practical Summary

- Trees and granite rocks are object-pass sprites, not terrain overlays.
- Tree sprites come from map images 200-334 and shadows 350-484.
- Granite sprites come from map images 516-527 and shadows 616-627.
- Tree rendering is stateful and animated; granite rendering is a direct
  state-to-size cache lookup.
- Fogged trees and granite use remembered `FOWObject` snapshots.
- Minimap updates are needed when tree/granite presence changes, not when only
  tree animation or granite visual size changes.
