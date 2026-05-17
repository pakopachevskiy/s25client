# Storehouse Alternative Road Plan

## Goal

Make the AI always try to add a secondary road for newly built Storehouses, even when the new road is longer than the current road-network path. Keep the existing shortcut-only behavior for ordinary non-military buildings.

## Current Behavior

- `BuildJob::TryToBuildSecondaryRoad()` calls `AIConstruction::BuildAlternativeRoad(houseFlag, route)` for every non-military building, including `BuildingType::Storehouse`.
- `AIConstruction::BuildAlternativeRoad()` currently uses one policy for all callers:
  - search owned flags within radius `10`,
  - require the candidate flag to be connected to the road system,
  - compute the existing road-network distance to the building flag or to the main-road endpoint,
  - skip candidates whose straight-distance lower bound cannot beat the existing path,
  - build only when `newLength * 5 + bqPenalty.roadRoute * routeBQPenalty < oldLength`, or when no existing path is available.
- This makes secondary roads conservative shortcuts. A Storehouse with a valid alternate route that is longer than the existing route is rejected.

## Recommended Design

Add an explicit secondary-road policy and pass it from `BuildJob`, rather than inferring the building type from map objects around the flag.

```cpp
enum class AlternativeRoadPolicy
{
    ShortcutOnly,
    BuildFirstValid
};
```

Use `ShortcutOnly` for the current behavior and `BuildFirstValid` for `BuildingType::Storehouse`.

Rationale:

- `BuildJob` already knows the requested `BuildingType`, including while the building may still be a construction site.
- `AIConstruction::BuildAlternativeRoad()` remains reusable and testable without hardcoding flag-to-building-position assumptions.
- Existing behavior for non-storehouse jobs stays unchanged.

## Implementation Steps

1. Update `libs/s25main/ai/aijh/planning/AIConstruction.h`.
   - Add `AlternativeRoadPolicy` in the `AIJH` namespace near `AIConstruction`.
   - Change `BuildAlternativeRoad` to:

   ```cpp
   bool BuildAlternativeRoad(
       const noFlag* flag,
       std::vector<Direction>& route,
       AlternativeRoadPolicy policy = AlternativeRoadPolicy::ShortcutOnly);
   ```

2. Update `libs/s25main/ai/aijh/planning/Jobs.cpp`.
   - In `BuildJob::TryToBuildSecondaryRoad()`, choose the policy from `type`:

   ```cpp
   const auto policy = type == BuildingType::Storehouse
                         ? AlternativeRoadPolicy::BuildFirstValid
                         : AlternativeRoadPolicy::ShortcutOnly;
   if(aijh.GetConstruction().BuildAlternativeRoad(houseFlag, route, policy))
   ```

3. Update `libs/s25main/ai/aijh/planning/AIConstruction.cpp`.
   - Preserve all validity checks for both policies:
     - skip the main-road endpoint,
     - require candidate ownership via `FindFlags()`,
     - require `IsConnectedToRoadSystem(&curFlag)`,
     - require `FindFreePathForNewRoad()`,
     - reject routes with `GetMaxNonFlaggableRun(...) > 2`,
     - keep `BuildRoad(...)` as the final authority.
   - Apply the current shortcut lower-bound skip only for `ShortcutOnly`:

   ```cpp
   if(policy == AlternativeRoadPolicy::ShortcutOnly
      && pathAvailable
      && aii.gwb.CalcDistance(flag->GetPos(), curFlag.GetPos()) * lengthFactor >= oldLength)
       continue;
   ```

   - For weighted refinement:
     - keep the existing threshold for `ShortcutOnly`,
     - for `BuildFirstValid`, allow refinement for every valid candidate when weighted road search is enabled, because there may be no "close enough to oldLength" shortcut threshold.

   ```cpp
   const bool shouldRefine =
       useWeightedRefinement
       && (policy == AlternativeRoadPolicy::BuildFirstValid
           || !pathAvailable
           || effectiveNewLength < oldLength + bqPenaltyConfig.roadRouteWeightedRefinementScoreMargin);
   ```

   - Relax the final build condition for Storehouses:

   ```cpp
   const bool shouldBuild =
       !pathAvailable
       || policy == AlternativeRoadPolicy::BuildFirstValid
       || effectiveNewLength < oldLength;
   ```

   - Keep the function returning after the first successfully built alternative road. That matches the current candidate-order behavior and limits extra pathfinder work.

4. Do not add a configuration knob unless gameplay tuning needs it later.
   - The requested behavior is specific and unconditional for Storehouses.
   - Adding YAML config now would expand the behavior surface and require parser/docs/tests without a clear need.

## Tests

Add focused regression coverage in `tests/s25Main/integration/testAI.cpp` or a nearby integration test file.

Recommended tests:

1. `BuildAlternativeRoad_StorehousePolicyBuildsLongerValidRoad`
   - Create or arrange a player-owned road network with a building flag and a connected candidate flag.
   - Ensure an existing road path is available and shorter than `newLength * 5 + BQ penalty`.
   - Call `BuildAlternativeRoad(..., AlternativeRoadPolicy::BuildFirstValid)`.
   - Assert it returns `true` and issues/builds the road route.

2. `BuildAlternativeRoad_ShortcutPolicyRejectsLongerRoad`
   - Use the same setup.
   - Call `BuildAlternativeRoad(..., AlternativeRoadPolicy::ShortcutOnly)`.
   - Assert it returns `false`.

3. `BuildJob_StorehouseUsesBuildFirstValidPolicy`
   - Prefer this only if the fixture can execute a `BuildJob` cheaply.
   - Otherwise, the policy-level tests above are enough and avoid brittle full-AI scheduling.

If constructing a full road-network fixture is too costly, keep the tests at the `AIConstruction::BuildAlternativeRoad` policy level and avoid broad autoplay tests.

## Documentation Updates

Update `docs/ai/road-route-selection.md`:

- In `Secondary Roads`, state that ordinary non-military buildings still use the shortcut-only rule.
- Add that Storehouses use a special policy: the AI will try to build the first valid secondary road to a connected nearby flag even when the new segment is longer than the current road-network path.
- Keep the existing formula documented as the non-storehouse rule.

Optionally update `docs/ai/construction-mechanics.md` where it summarizes `TryToBuildSecondaryRoad()`.

## Risks And Guardrails

- Storehouses may build longer local loops and consume more boards/stones. This is intended by the request, but the behavior should stay limited to Storehouses.
- Do not bypass route validity checks. Longer roads must still be road-buildable, player-owned, compatible with flag placement, and accepted by `BuildRoad`.
- Avoid making `BuildAlternativeRoad()` infer Storehouse status from `flag->GetPos()` neighbors. During construction, the adjacent object may be a site rather than the final warehouse.
- Do not enable this for military buildings; they currently skip secondary roads entirely.

## Acceptance Criteria

- Storehouse secondary-road attempts are not rejected solely because the candidate road is longer than the existing road-network path.
- Non-storehouse secondary roads retain the current shortcut-only threshold.
- Weighted refinement still works for the Storehouse path before building when enabled.
- Regression tests cover both the Storehouse policy and the unchanged shortcut policy.
- `docs/ai/road-route-selection.md` describes the new Storehouse exception.
