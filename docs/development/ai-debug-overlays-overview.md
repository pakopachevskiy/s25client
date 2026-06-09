# AI Debug Overlays Overview

This note summarizes the overlays exposed by the in-game AI Debug window and
where each overlay gets its data. Detailed rendering notes for selected
overlays live in the neighboring development docs.

## Window Entry Point

The AI Debug window is implemented by
`libs/s25main/ingameWindows/iwAIDebug.cpp`. It is opened through the in-game
main menu's `AI` button. In release builds the button is available to the host
when the `AI_DEBUG_WINDOW` addon is enabled; in non-release builds it is
always available to the host.

`iwMainMenu` passes active AI players to `iwAIDebug`. The window keeps only
players that implement `AIJH::AIDebugView`, so the UI reads through a small
debug interface instead of depending directly on the full AI implementation.

## Overlay Types

The combo-box order in `iwAIDebug` is also the internal overlay id:

| ID | Name | Rendering path | Main data source |
| ---: | --- | --- | --- |
| 0 | None | no overlay | none |
| 1 | BuildingQuality | map icon per node | `AIDebugView::GetAINode(pt).bq` |
| 2 | Reachability | tick/cross icon per node | `AIDebugView::GetAINode(pt).reachable` |
| 3 | Farmed | tick/cross icon per node | `AIDebugView::GetAINode(pt).farmed` |
| 4 | Gold | map text per node | `GetResourceValueForDebug(pt, AIResource::Gold)` |
| 5 | Ironore | map text per node | `GetResourceValueForDebug(pt, AIResource::Ironore)` |
| 6 | Coal | map text per node | `GetResourceValueForDebug(pt, AIResource::Coal)` |
| 7 | Granite | map text per node | `GetResourceValueForDebug(pt, AIResource::Granite)` |
| 8 | Fish | map text per node | `GetResourceValueForDebug(pt, AIResource::Fish)` |
| 9 | Wood | map text per node | `GetResourceValueForDebug(pt, AIResource::Wood)` |
| 10 | Stones | map text per node | `GetResourceValueForDebug(pt, AIResource::Stones)` |
| 11 | Plantspace | map text per node | `GetResourceValueForDebug(pt, AIResource::Plantspace)` |
| 12 | Borderland | map text on owned nodes | `GetResourceValueForDebug(pt, AIResource::Borderland)` |
| 13 | Position rating | map text on owned nodes | `AIDebugView::GetPointRating(buildingType, pt)` |
| 14 | Buildings wanted | debug text panel | `AIDebugView::GetNumBuildingsWanted(type)` |
| 15 | Inventory | debug text panel | selected `AIPlayer`'s `GamePlayer::GetInventory()` |
| 16 | Road Workload | map text on road interior nodes | `AIDebugView::GetRoadWorkload(pt)` |
| 17 | Global Build Queue | debug text panel | `AIDebugView::GetGlobalBuildJobs()` |

IDs below `13` are intentionally compact: resource overlays are mapped by
`AIResource(overlay - 4)`.

## Map-Draw Overlays

Map-draw overlays are rendered by `iwAIDebug::DebugPrinter`, an
`IDrawNodeCallback` registered with `GameWorldView`. The callback runs during
the normal visible-node draw loop, after terrain, visible objects, fog
snapshots, figures, and construction-aid rendering.

These overlays do not alter AI state. They render the selected AI's latest
cached debug values. Some values can therefore be stale until the AI refreshes
its planning cache.

The `Position rating` overlay is the only overlay with an extra building-type
combo box. Changing the selected building type updates
`DebugPrinter::buildingType`, and each owned node asks the AI for a rating for
that building type.

## Text-Panel Overlays

Text-panel overlays are rendered in `iwAIDebug::Msg_PaintBefore()`. They use
the same multiline control that otherwise shows the selected AI's current job.
`SetTextIfChanged()` avoids clearing and repopulating the control when the
rendered content did not change.

`Buildings wanted` lists build planner demand by building type, skipping
disabled entries. `Inventory` lists the selected player's goods inventory.
`Global Build Queue` lists every queued global build job as:

```text
Building name: priority
```

The global queue is exposed as a read-only snapshot from
`AIConstruction::GetGlobalBuildJobs()`. Queue order follows the construction
multiset order, which means higher-priority jobs appear first.

## Related Notes

- `buildquality-ai-debug-overlay-rendering.md` explains the
  `BuildingQuality` icon overlay in detail.
- `borderland-ai-debug-overlay-rendering.md` explains the `Borderland`
  resource overlay.
- `road-workload.md` explains the `Road Workload` overlay and its AI bypass
  use.
- `building-and-construction-site-rendering.md` covers normal building,
  construction-site, and productivity overlay rendering outside the AI Debug
  window.

