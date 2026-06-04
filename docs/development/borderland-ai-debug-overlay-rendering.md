# Borderland AI Debug Overlay Rendering

This note describes how the AI Debug window renders its `Borderland` overlay.
The overlay visualizes the selected AI player's Borderland resource rating for
owned map plots.

## What The Overlay Shows

`Borderland` is one of the resource overlays in the AI Debug window. It is
represented internally by overlay id `12`, after the other resource entries:

```text
Gold, Ironore, Coal, Granite, Fish, Wood, Stones, Plantspace, Borderland
```

When selected, the overlay draws a numeric Borderland rating over each visible
map plot owned by the selected AI player. Unowned and enemy plots are skipped:
their rating is not calculated and no text is drawn.

The ownership filter matters for both presentation and cost. Borderland is
useful for choosing construction sites inside the selected player's territory.
Calculating and drawing values outside that territory adds noise without
helping to inspect those decisions.

## Rendering Path

`iwAIDebug` owns a `DebugPrinter` callback registered with
`GameWorldView::AddDrawNodeCallback(...)`. `GameWorldView` calls that callback
for each visible map node after drawing the normal world objects.

For resource overlay ids `4` through `12`, `DebugPrinter::onDraw(...)` converts
the overlay id into an `AIResource`. Borderland has an additional ownership
guard:

```cpp
const AIResource resource = AIResource(overlay - 4);
if(resource != AIResource::Borderland || ai->GetAINode(pt).owned)
    font.Draw(curPos, helpers::toString(ai->GetResourceValueForDebug(pt, resource)),
              FontStyle{}, COLOR_YELLOW);
```

Because the ownership check is evaluated before
`GetResourceValueForDebug(...)`, non-owned Borderland plots do not trigger a
calculation.

## Live Value Source

Most resource overlays display the corresponding stored `AIResourceMap`
heatmap value. Borderland is intentionally different:

```cpp
int AIPlayerJH::GetResourceValueForDebug(const MapPoint pt,
                                         const AIResource res) const
{
    if(res == AIResource::Borderland)
        return aii.Queries().CalcResourceValue(pt, res);
    return GetResMapValue(pt, res);
}
```

Borderland uses `AIQueryService::CalcResourceValue(...)`, which is also the
query path used by current global AI placement logic. This makes the overlay
show the value relevant to actual AI decisions.

The dedicated debug accessor preserves the existing behavior of
`GetResMapValue(...)`. Legacy AI code that explicitly reads an
`AIResourceMap` still receives its stored heatmap values.

## Why The Stored Heatmap Is Not Used

`AIResourceMap` classifies Borderland as a replenishable resource. During
resource-map initialization, replenishable maps are resized but are not fully
calculated. Their values are populated locally when older search paths call
`AIResourceMap::updateAround(...)`.

As a result, the stored Borderland heatmap can contain zeros at the beginning
of a match and can remain incomplete or stale when global construction
searches bypass it.

The debug overlay therefore reads the live query-service value rather than the
stored heatmap snapshot.

## Borderland Calculation

`AIQueryService::CalcResourceValue(pt, AIResource::Borderland)` sums the
Borderland rating of plots within `RES_RADIUS[AIResource::Borderland]`, which
is currently `5`.

For each plot in that radius:

- owned non-border plots contribute `0`
- border plots and plots outside the player's territory contribute `5` when
  either terrain triangle is walkable
- non-walkable plots contribute `0`

The result is higher near the edge of the selected player's territory and
lower deeper inside it.

## Caching And Cost

The overlay uses the normal `AIQueryService` resource-value cache. Borderland
entries have a cache TTL of `30,000` game frames.

The first visible draw after enabling the overlay, or after panning into a new
area, may calculate previously unseen owned plots. Subsequent draws generally
reuse cached values. Territory ownership changes invalidate nearby Borderland
cache entries so the overlay and AI planning can recalculate affected plots.

The renderer only requests values for visible owned plots. It does not
calculate the entire map when the overlay is selected.

## Relationship To AI Placement

Global AI placement queries Borderland directly through
`AIQueryService::CalcResourceValue(...)`. Military building location ratings
use Borderland as their configured resource score. Non-military placement also
uses Borderland when reserving suitable border-adjacent slots for military
buildings.

The overlay is observational only. Selecting it does not change placement
rules, ownership, territory borders, or resource-map state, apart from warming
the normal query-service cache.

## Practical Trace

The important files are:

- `libs/s25main/ingameWindows/iwAIDebug.cpp`: filters owned plots and draws
  numeric overlay text
- `libs/s25main/ai/aijh/debug/AIDebugView.h`: declares the debug resource-value
  accessor
- `libs/s25main/ai/aijh/runtime/AIPlayerJHMapState.cpp`: routes Borderland
  debug reads to the cached live query
- `libs/s25main/ai/AIQueryService.cpp`: calculates, caches, and invalidates
  Borderland values
- `libs/s25main/ai/AIResource.h`: defines the Borderland radius
- `libs/s25main/ai/aijh/runtime/AIResourceMap.cpp`: contains the legacy stored
  heatmap behavior

The shortest rendering chain is:

```text
iwAIDebug::DebugPrinter::onDraw(pt, curPos)
  -> skip when Borderland is selected and !ai->GetAINode(pt).owned
  -> ai->GetResourceValueForDebug(pt, AIResource::Borderland)
  -> AIPlayerJH::GetResourceValueForDebug(...)
  -> AIQueryService::CalcResourceValue(...)
  -> cached live Borderland rating
  -> font.Draw(...)
```
