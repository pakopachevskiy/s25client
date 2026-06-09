# Map Highlights

This note describes map-view highlight features drawn by `GameWorldView`.
These overlays are visual aids only. They do not change gameplay state or issue
commands.

## Draw Path

`libs/s25main/world/GameWorldView.cpp` draws the map in two broad phases:

- `Draw(...)` scans visible map nodes and draws terrain, boundary stones,
  objects, figures, construction aid, and name/productivity overlays.
- `DrawGUI(...)` draws interactive map overlays after the world objects, using
  the current hovered node, selected node, and road-building state.

Highlight features live in `DrawGUI(...)` because they are view-local UI
feedback rather than world objects.

## Worker Radius Highlight

When the mouse hovers a visible finished production building, the view checks
whether the building has a worker search radius:

```text
GetBuildingWorkerRadius(building->GetBuildingType())
```

The radius data comes from `libs/s25main/gameData/BuildingConsts.h`.
Buildings whose workers actively search nearby terrain, such as woodcutters,
foresters, fishers, hunters, farmers, winegrowers, charburners, and
stonemasons, return a non-zero radius. Other buildings return `0` and do not
show this overlay.

For each visible map node inside the radius, except the building node itself,
`DrawGUI(...)` draws map texture `20` with a translucent green color:

```text
SetAlpha(COLOR_GREEN, 0x55)
```

This paints the affected map tiles without changing the building sprites.

## Protected Building Highlight

When the mouse hovers a visible `nobMilitary`, the view highlights the
non-military buildings that would be lost if that military building were
captured.

The protected-building list is computed through:

```text
GameWorld::GetBuildingObjectsLostOnCapture(militaryBuilding)
```

This reuses the same capture-loss simulation as
`GameWorld::GetBuildingsLostOnCapture(...)`, which is also used by AI
protection-value scoring. The simulation recalculates local territory as if
the hovered military building had been captured, then returns buildings that
would become destruction candidates under the changed ownership.

`GameWorldView` caches the highlighted building positions for the currently
hovered military building and recomputes them only when the hovered map point
changes. If the hovered object is not a military building, the cache is cleared.

Only non-military protected buildings are highlighted:

```text
!BuildingProperties::IsMilitary(protectedBuilding->GetBuildingType())
```

For each visible protected building, `DrawGUI(...)` redraws the building's own
sprite with a translucent green color:

```text
protectedBuilding->GetBuildingImage().DrawFull(curPos, SetAlpha(COLOR_GREEN, 0x66))
```

Because the building texture is reused, the green shade follows the sprite's
non-transparent pixels instead of tinting the whole map tile.

## Existing Selection And Road Highlights

`DrawGUI(...)` also handles existing map interaction overlays:

- mouse cursor tile marker
- currently selected map point marker
- current road-building route point
- valid next road segment markers
- target-flag markers while extending roads
- maximum waterway-length filtering for boat roads

These overlays are separate from hover highlights but share the same late GUI
draw pass, so they appear above terrain and world objects.
