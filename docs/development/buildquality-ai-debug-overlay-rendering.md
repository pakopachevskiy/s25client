# BuildQuality AI Debug Overlay Rendering

This note describes how the AI Debug window renders its `BuildingQuality`
overlay. It complements the normal building and construction-site rendering
path described in `docs/development/building-and-construction-site-rendering.md`.

## What The Overlay Is

The `BuildingQuality` entry in the AI Debug window is a debug overlay, not a
building renderer and not terrain rendering. It draws one small map icon at
each visited map node when the selected AI's cached node state says that a
building or flag can be placed there.

The overlay is opened through the in-game main menu's `AI` button. In release
builds that button is shown only when the host has enabled the
`AI_DEBUG_WINDOW` addon; in non-release builds it is always available to the
host. `iwMainMenu` collects the active AI players and opens `iwAIDebug`.

Only AI players that implement `AIJH::AIDebugView` are exposed in the debug
window. The `BuildingQuality` overlay is the second combo-box entry after
`None`, so it is represented internally by overlay id `1`.

## Callback Registration

`iwAIDebug` owns an inner `DebugPrinter` class that implements
`IDrawNodeCallback`.

When the debug window is created, it:

1. filters the provided AI players to `AIJH::AIDebugView`
2. creates player and overlay combo boxes
3. creates one `DebugPrinter`
4. registers that printer with `GameWorldView::AddDrawNodeCallback(...)`

When the debug window is destroyed, it removes the same printer from
`GameWorldView` and deletes it. Switching the selected player or selected
overlay mutates the existing callback; it does not install another callback.

## Draw Order

`GameWorldView::Draw(...)` renders the world in visible map rows. For each
screen node position it:

1. converts the row/column scan coordinate to a wrapped `MapPoint`
2. computes the node's screen-space `DrawPoint`
3. draws the boundary stone
4. draws the visible live object, moving figures, figures, and optional
   construction aid, or draws the remembered fog-of-war object
5. calls every registered `IDrawNodeCallback`

Because the AI debug callback is invoked after the visible/fog branch, the
`BuildingQuality` icon is drawn above terrain, objects, figures, fog memories,
and the normal construction-aid icon. It is still part of the world-view draw
pass, so it uses the same scissor, scroll offset, map wrapping, and zoom
projection as the terrain and object pass.

The callback itself does not check fog-of-war or player visibility. The values
it draws are whatever the selected AI exposes through `GetAINode(pt).bq`.

## Icon Selection

`DebugPrinter` caches the map textures once in its constructor:

- `BuildingQuality::Nothing` -> no texture
- `BuildingQuality::Flag` -> `LOADER.GetMapTexture(50)`
- `BuildingQuality::Hut` -> `LOADER.GetMapTexture(51)`
- `BuildingQuality::House` -> `LOADER.GetMapTexture(52)`
- `BuildingQuality::Castle` -> `LOADER.GetMapTexture(53)`
- `BuildingQuality::Mine` -> `LOADER.GetMapTexture(54)`
- `BuildingQuality::Harbor` -> `LOADER.GetMapTexture(55)`

For overlay id `1`, `DebugPrinter::onDraw(...)` reads:

```cpp
auto* img = bqImgs[ai->GetAINode(pt).bq];
if(img)
    img->DrawFull(curPos);
```

`Nothing` therefore draws nothing. Every other value draws the full cached
texture at the node's current screen position. The overlay uses the same map
texture ids as `GameWorldView::DrawConstructionAid(...)`, but the data source
is different.

## Data Source

The overlay reads from the selected AI's cached `AIJH::AIMap`. Each
`AIJH::Node` stores:

- `bq`: the cached `BuildingQuality`
- `owned`: whether the node is the AI player's territory
- `reachable`, `border`, and `farmed`: other debug overlays and planning state
- `res`: cached AI resource classification

The initial cache is built by `AIMapState::InitNodes()`, which stores
`owner_.aii.GetBuildingQuality(pt)` for every map point. That query forwards to
`AIQueryService::GetBuildingQuality(pt)`, which calls
`gwb.GetBQ(pt, playerID_)` on the `GameWorldBase` instance.

The inherited `World::GetBQ(...)` implementation adjusts the raw world node
`bq` for the AI player. In particular, non-owned territory returns
`BuildingQuality::Nothing`, and larger building qualities can be reduced or
hidden by the normal ownership and neighboring-territory rules. As a result,
the debug overlay normally shows the selected AI's buildable positions, not the
raw imported map build-quality layer.

## Cache Refresh

The AI cache is not recomputed directly from the renderer. It is maintained by
AI runtime state:

- `AIMapState::InitNodes()` fills the complete map when the AI is constructed.
- `AIMapState::UpdateNodesAround(...)` refreshes local state around known AI
  changes.
- `AIPlayerJH` subscribes to `NodeNote::BQ` and `NodeNote::Owner`
  notifications and records affected points in
  `AIMapState::GetNodesWithOutdatedBQ()`.
- Every 25 game frames, `AIPlayerJH::RunGF(...)` calls
  `AIMapState::RefreshBuildingQualities()`, which deduplicates the pending
  points and updates their cached `bq` values.

This means the overlay can briefly show the selected AI's previous cached
build-quality state until the next AI refresh tick. It can also differ from
the player's normal construction aid because construction aid reads
`GameWorldViewer::GetBQ(pt)` for the current viewer, while the AI debug overlay
reads the selected AI's cached `AIJH::Node::bq`.

## Relationship To Other Building Rendering

The overlay does not draw finished buildings, construction sites, foundations,
door sprites, ware stacks, or fog building snapshots. Those are handled by the
normal object and fog rendering paths.

The overlay also does not change where construction is allowed. It is a visual
inspection layer over AI planning state. Actual buildability still comes from
the world build-quality calculation and placement checks used by commands,
`World::GetBQ(...)`, `BQCalculator`, and the AI query/planning code.

## Practical Trace

For debugging or modifying this overlay, the important files are:

- `libs/s25main/ingameWindows/iwMainMenu.cpp`: opens the AI Debug window
- `libs/s25main/ingameWindows/iwAIDebug.cpp`: registers `DebugPrinter`, maps
  `BuildingQuality` values to map textures, and draws the overlay
- `libs/s25main/world/GameWorldView.cpp`: calls node draw callbacks during the
  map row traversal
- `libs/s25main/world/GameWorldView.h`: defines `IDrawNodeCallback`
- `libs/s25main/ai/aijh/debug/AIDebugView.h`: exposes AI debug data to the UI
- `libs/s25main/ai/aijh/runtime/AIMap.h`: defines the cached AI node data
- `libs/s25main/ai/aijh/runtime/AIMapState.cpp`: initializes and refreshes AI
  `BuildingQuality` cache entries
- `libs/s25main/ai/AIQueryService.cpp`: forwards AI build-quality queries to
  the game world

The shortest rendering chain is:

```text
iwMainMenu AI button
  -> iwAIDebug
  -> iwAIDebug::DebugPrinter registered with GameWorldView
  -> GameWorldView::Draw node loop
  -> DebugPrinter::onDraw(pt, curPos)
  -> ai->GetAINode(pt).bq
  -> bqImgs[bq]->DrawFull(curPos)
```
