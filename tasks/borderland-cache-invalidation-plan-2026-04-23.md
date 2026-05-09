# Targeted `Borderland` Resource-Value Cache Invalidation

## Goal

Invalidate cached `CalcResourceValue(pt, AIResource::Borderland)` entries in
`AIQueryService` immediately when the player's territory changes, rather than
waiting for the 1,000-game-frame TTL to expire.

Trigger events (from `docs/gameplay/military/nob-military.md`):

- A military building finishes and first occupies its garrison — fires
  `RecalcTerritory(..., TerritoryChangeReason::Build)`.
- A military building is destroyed — fires
  `RecalcTerritory(..., TerritoryChangeReason::Destroyed)`.
- A military building is captured — fires
  `RecalcTerritory(..., TerritoryChangeReason::Captured)`.

Harbor building sites on first occupation also funnel through `RecalcTerritory`.

## Why only `Borderland`?

`AIQueryService::GetResourceRating(pt, AIResource::Borderland)` is the only
rating whose result flips on ownership/border changes:

```cpp
case AIResource::Borderland:
    if(IsOwnTerritory(pt) && !IsBorder(pt))
        return 0;
    else {
        // walkable terrain check
        return RES_RADIUS[res];
    }
```

All other `AIResource` ratings depend on map geometry, subsurface deposits,
surface objects, or building occupants — none of which territory recalc alters.
So territory changes only stale `Borderland` entries; other resources keep
their cached values.

## Where the staleness lives

`CalcResourceValue(center, Borderland)` sums `GetResourceRating` over the
radius-5 disc (`RES_RADIUS[Borderland] = 5`) centered at `center`. If point `p`
has its owner or border status flipped, every cached `(center, Borderland)`
with `distance(center, p) <= 5` is stale.

A point's `IsBorder` status can also flip when a *neighbor*'s owner changes
(because border stones propagate one step). To cover that case we invalidate
a disc of radius `RES_RADIUS[Borderland] + 1 = 6` around every changed owner
point. This remains bounded and avoids enumerating the exact neighbor set.

## Hook site: `NodeNote::Owner` subscription per-AI

`GameWorld::RecalcTerritory` (`libs/s25main/world/GameWorld.cpp:498-499`)
already publishes `NodeNote(NodeNote::Owner, pt)` for each point whose owner
changed. `AIPlayerJH::recordBQsToUpdate` already subscribes to `NodeNote` for
BQ bookkeeping (`libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp:115-133`), so the
wiring pattern is established.

Each AI owns its own `AIQueryService` through `AIInterface::queryService_`
(`libs/s25main/ai/AIInterface.h:97`), so each AI invalidates only its own
cache. `NodeNote::Owner` is published for every territory change in the world,
so every AI's subscription fires — this is correct: any player's military
action can flip the `IsBorder` / `IsOwnTerritory` result for another AI.

## Implementation

### 1. `AIQueryService` — add targeted invalidation API

`libs/s25main/ai/AIQueryService.h` — add to the public section:

```cpp
/// Erase cached CalcResourceValue entries for `res` whose key-point lies
/// within `radius` steps of `center`. No-op if the cache is empty or the
/// resource was never cached at a matching point.
void InvalidateResourceValueInRadius(MapPoint center, AIResource res, unsigned radius);
```

`libs/s25main/ai/AIQueryService.cpp` — implement:

```cpp
void AIQueryService::InvalidateResourceValueInRadius(
    const MapPoint center, const AIResource res, const unsigned radius)
{
    if(resourceValueCache_.empty())
        return;
    // For small radii the disc is much smaller than the cache, so enumerate
    // candidate keys rather than scanning the whole map.
    for(const MapPoint& pt : gwb.GetPointsInRadiusWithCenter(center, radius))
        resourceValueCache_.erase(CacheKey{pt, res});
}
```

Rationale for enumerating the disc instead of scanning the cache:
`unordered_map::erase(key)` is O(1); a disc of radius 6 has 127 points, which
is tiny versus the cache size (potentially 200k entries).

### 2. `AIPlayerJH` — subscribe and invalidate

Extend the existing `NodeNote` handling. Two clean options:

**Option A (preferred): a second subscription function next to `recordBQsToUpdate`.**
Keeps the BQ bookkeeping untouched and makes the invalidation intent visible.

`libs/s25main/ai/aijh/runtime/AIPlayerJH.h` — declare:

```cpp
Subscription subscribeOwnerChangesToInvalidateBorderlandCache(
    const GameWorldBase& gw, AIQueryService& queries);
```

`libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp`:

```cpp
Subscription subscribeOwnerChangesToInvalidateBorderlandCache(
    const GameWorldBase& gw, AIQueryService& queries)
{
    constexpr unsigned kInvalidationRadius = RES_RADIUS[AIResource::Borderland] + 1;
    return gw.GetNotifications().subscribe<NodeNote>(
        [&queries, kInvalidationRadius](const NodeNote& note) {
            if(note.type != NodeNote::Owner)
                return;
            queries.InvalidateResourceValueInRadius(
                note.pos, AIResource::Borderland, kInvalidationRadius);
        });
}
```

Add a member `Subscription subBorderlandInvalidate;` in `AIPlayerJH` and wire
it up next to `subBQ` in the constructor:

```cpp
subBorderlandInvalidate =
    subscribeOwnerChangesToInvalidateBorderlandCache(this->gwb, aii.Queries());
```

**Option B:** fold the call into the existing `recordBQsToUpdate` lambda at the
`NodeNote::Owner` branch. Shorter but couples cache invalidation to BQ
bookkeeping, which obscures intent — prefer A.

### 3. No changes needed in `GameWorld::RecalcTerritory`

The existing `NodeNote::Owner` publications (one per changed point) are enough.
Per-point invalidation on a radius-6 disc means some discs overlap between
neighboring changed points, but the redundant `unordered_map::erase` calls are
cheap and no correctness issue arises.

### 4. Raise the `Borderland` TTL once invalidation is in place

With targeted invalidation wired up, the 1,000-game-frame TTL on `Borderland`
(~40 s at 25 GF/s) becomes overly conservative — its sole purpose was to bound
staleness from territory shifts, and that signal is now delivered precisely.

Bump the TTL to **30,000 game frames** (~20 min at 25 GF/s):

- `libs/s25main/ai/AIQueryService.cpp` — add `constexpr unsigned kBorderlandTTL = 30'000;`
  and return it from `GetCacheTTL` for `AIResource::Borderland` instead of
  falling through to `kDefaultTTL`:

  ```cpp
  case AIResource::Borderland:
      return kBorderlandTTL;
  ```

- `docs/ai/resource-value-cache.md` — update the per-resource TTL table:
  `Borderland | any | 30,000 | Invalidated on territory changes; TTL is a
  safety net`.

The TTL now acts only as a backstop against missed events or long-idle
entries; actual freshness is maintained by the invalidation hook. This should
noticeably raise `Borderland` hit rates during steady-state planning, where
territory is static between military actions.

### Ordering

Implement and merge steps 1–3 (API + subscription + tests) **before** step 4.
Raising the TTL without invalidation would visibly stale out `Borderland`
values and mis-classify frontier build slots for minutes at a time.

## Testing

### Unit tests in `tests/s25Main/integration/testAIQueryServiceCache.cpp`

Add to the existing suite:

1. **`InvalidateResourceValueInRadius_EvictsMatchingBorderlandEntries`**
   - Warm cache with `CalcResourceValue(center, Borderland)` at several
     points.
   - Call `InvalidateResourceValueInRadius(p, Borderland, 6)`.
   - Verify points within radius-6 of `p` miss on re-query; points outside
     hit.

2. **`InvalidateResourceValueInRadius_OnlyAffectsRequestedResource`**
   - Warm cache with multiple resources at the same point.
   - Invalidate `Borderland` at that point with radius 6.
   - Verify `Borderland` re-query misses while `Wood`, `Stones`, etc. still
     hit.

3. **`InvalidateResourceValueInRadius_NoopOnEmptyCache`**
   - Call on fresh service; assert no crash, stats unchanged.

### Integration test — ownership change triggers invalidation

New test (same file or `testAIPlayerJH`-style harness) using the
`WorldWithGCExecution`-style fixture that drives real game commands:

- Place a player HQ, warm `CalcResourceValue(pt, Borderland)` for several
  points near the frontier.
- Build and finish a military building so `RecalcTerritory` runs with
  `TerritoryChangeReason::Build`.
- Verify cached `Borderland` entries within radius 6 of any owner-changed
  point are gone (next query is a miss), while `Borderland` entries further
  away and non-`Borderland` entries still hit.

Capture and destroy paths can be covered by two additional scenarios built on
the same fixture, driving `Destroy`/`Capture` through the usual test helpers.

## Risk & scope notes

- **Bounded work per event**: radius-6 disc = ~127 points, each an O(1)
  erase. Even a large territory shift publishing hundreds of owner notes is
  cheap relative to one uncached `CalcResourceValue` (which itself touches
  ~127 points).
- **Correctness guaranteed by the hook location**: `NodeNote::Owner` is
  published for *every* owner flip, so no territory change can leak stale
  values.
- **No behavior change for other resources**: the targeted eviction only
  touches `Borderland` keys; `Fish`, deposits, `Wood`, `Plantspace`, etc.
  remain governed by their TTLs.
- **No change to hit/miss counters**: invalidation removes entries but
  doesn't touch `resourceValueCacheHits_`/`Misses_`, so cache-effectiveness
  stats stay meaningful across territory events.
- **Lazy eviction still runs**: unrelated to this path; the periodic TTL sweep
  in `MaybeEvictExpired` continues to clean up `Borderland` keys outside the
  invalidation disc once their TTL lapses.

## File-by-file checklist

| File | Change |
|------|--------|
| `libs/s25main/ai/AIQueryService.h` | Declare `InvalidateResourceValueInRadius` |
| `libs/s25main/ai/AIQueryService.cpp` | Implement `InvalidateResourceValueInRadius`; add `kBorderlandTTL = 30'000` and return it from `GetCacheTTL` |
| `libs/s25main/ai/aijh/runtime/AIPlayerJH.h` | Declare helper + `Subscription` member |
| `libs/s25main/ai/aijh/runtime/AIPlayerJH.cpp` | Implement subscription helper; wire it up in ctor |
| `tests/s25Main/integration/testAIQueryServiceCache.cpp` | Add unit + integration tests |
| `docs/ai/resource-value-cache.md` | Add short section on targeted `Borderland` invalidation; update TTL table |
