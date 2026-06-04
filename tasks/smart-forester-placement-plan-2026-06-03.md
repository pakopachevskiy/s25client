# Smart Forester Placement Plan

## Summary

Make Foresters prefer renewable-wood zones that are poor for normal construction, instead of consuming valuable
`House`, `Castle`, or `Harbor` land. A good Forester site should have plenty of plantable terrain, mostly `Flag` or
`Hut` build quality nearby, enough hut-capable spots for Woodcutters, and support clustered Foresters only when the
low-value area is large enough.

## Key Changes

1. Fix proximity rule handling in `GlobalPositionFinder::CheckProximity()`.
   - Evaluate all enabled proximity rules instead of returning after the first enabled rule.
   - This makes the config intent work: most building types avoid Foresters, while Woodcutters remain allowed near them.
   - Reduce `posFinder.Forester.proximity.Forester` from `11` to a smaller spacing such as `4`, so multiple Foresters
     can share a large forest zone without overlapping too tightly.

2. Add Forester-specific zone scoring in `GlobalPositionFinder::GetPointRating()`.
   - Keep existing hard filters: owned, reachable, correct BQ for Forester, not farmed, not border-blocked, and enough
     `Plantspace`.
   - For each Forester candidate, scan radius `6`, matching the Forester work radius.
   - Count exact plantable points using rules equivalent to `nofForester::GetPointQuality()` without worker
     path/reservation checks.
   - Count low-value plots (`Flag`, `Hut`), high-value plots (`House`, `Castle`, `Harbor`, `Mine`), nearby existing
     Foresters, and hut-capable Woodcutter sites.

3. Apply Forester-specific thresholds before scoring.
   - Reject candidates with fewer than `18` plantable points in radius `6`.
   - Reject candidates where less than `60%` of plantable points are `Flag` or `Hut`.
   - Reject candidates where more than `20%` of plantable points are `House`, `Castle`, `Harbor`, or `Mine`.
   - Reject candidates with fewer than `2` valid nearby Woodcutter sites, unless there are already Woodcutters in range.

4. Score low-value central forest zones higher.
   - Suggested per-plot values:
     - `Flag: +8`
     - `Hut: +6`
     - `Nothing: +1`
     - `House: -8`
     - `Mine: -8`
     - `Castle: -16`
     - `Harbor: -18`
   - Add `+2` per plantable point, so large contiguous plantable zones naturally prefer the center.
   - Allow up to `3` Foresters in one zone only when there are at least `18` plantable points per existing-or-new
     Forester; otherwise penalize nearby Foresters.
   - Keep Woodcutter's existing `rating.Forester` behavior, but reduce or remove Forester's bonus for existing
     Woodcutters so Foresters create forest zones first and Woodcutters follow.

5. Queue Woodcutters after Forester placement.
   - Re-enable the currently commented Forester chain in `Jobs.cpp`.
   - After a Forester site is successfully placed and connected, enqueue a Woodcutter build job around that Forester
     only if `Wanted(Woodcutter)` is true.
   - Do not force Woodcutters beyond demand; global planning and existing `Woodcutter.rating.Forester` should place
     more while demand remains.

## Config And Interface Additions

Add optional nested config under `posFinder.Forester`:

```yaml
smartForest:
  enabled: true
  radius: 6
  minPlantable: 18
  minLowValueRatio: 0.60
  maxHighValueRatio: 0.20
  minWoodcutterSites: 2
  maxClusterForesters: 3
  plantablePerForester: 18
```

Defaults should enable the feature for Foresters only. Other building placement remains unchanged except for the
proximity-rule bug fix.

## Test Plan

- Unit-test Forester zone metrics on synthetic maps:
  - Castle-heavy plantable area is rejected or scored low.
  - Flag/Hut-heavy plantable area is accepted and scored high.
  - Plantable area with no hut-capable Woodcutter spots is rejected.
- Placement regression tests:
  - Forester chooses the center of a large low-value patch over the edge.
  - A second Forester is allowed in a large enough patch.
  - A second Forester is rejected in a small patch.
- Config/parser test for `smartForest`.
- Integration smoke test with AI startup: it still builds the initial wood chain, but Forester no longer claims
  high-value construction land when a lower-value area exists.

## Source Notes

- Forester plantability is based on `nofForester::GetPointQuality()`: free point, no boundary stone, no road, no
  adjacent building, and all surrounding terrain triangles are vital.
- Vital terrain is `TerrainKind::Land && ETerrain::Buildable`.
- Building-quality semantics are documented in `docs/gameplay/building-quality.md`.
- Existing global placement is implemented in `GlobalPositionFinder`.
- Existing Woodcutter placement already prefers nearby Foresters via `posFinder.Woodcutter.rating.Forester`.
