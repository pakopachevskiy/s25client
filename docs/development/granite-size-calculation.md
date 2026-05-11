# Granite Size Calculation

This note summarizes how RTTR stores, renders, decrements, and logs granite
rock size.

## Three Related Values

Granite has three size-like values that should not be confused:

- raw state: the internal durability counter stored in `noGranite::state`
- visual size: the sprite-cache index used for rendering
- remaining stones: the event-log value representing how many stonemason
  actions are still available

The implementation lives in
[libs/s25main/nodeObjs/noGranite.cpp](../../libs/s25main/nodeObjs/noGranite.cpp)
and [libs/s25main/nodeObjs/noGranite.h](../../libs/s25main/nodeObjs/noGranite.h).

## Serialized State

`noGranite` stores one byte:

```cpp
unsigned char state;
```

That byte is split into:

- `BOOSTED_STATE_FLAG = 0x80`
- `RAW_STATE_MASK = 0x7F`

The public `GetSize()` accessor returns only the raw durability counter:

```cpp
state & RAW_STATE_MASK
```

So code that calls `GetSize()` never sees the boosted flag.

## Map-Load Encoding

Map loading creates granite in
[libs/s25main/world/MapLoader.cpp](../../libs/s25main/world/MapLoader.cpp).

Classic map object indices `0x01` through `0x07` are converted to legacy states
`0` through `6`, then passed through:

```cpp
noGranite::EncodeBoostedState(lc - 1)
```

`EncodeBoostedState(...)` preserves the legacy visual size but doubles the
available yield:

```cpp
rawBoostedState = 2 * (legacyState + 1) - 1
state = BOOSTED_STATE_FLAG | rawBoostedState
```

Examples:

- legacy state `0` becomes boosted raw state `1`
- legacy state `1` becomes boosted raw state `3`
- legacy state `5` becomes boosted raw state `11`
- legacy state `6` becomes boosted raw state `13`

The stored raw state is one less than the remaining-stone count. That is why a
raw state of `0` still represents one final stone that can be removed.

## Hewing Logic

Stonemasons reduce granite through `noGranite::Hew()`:

```cpp
const unsigned char rawState = state & RAW_STATE_MASK;
if(rawState == 0)
    return;

const unsigned char flags = state & BOOSTED_STATE_FLAG;
state = flags | (rawState - 1);
```

Important details:

- only the raw state is decremented
- `BOOSTED_STATE_FLAG` is preserved
- `Hew()` does nothing at raw state `0`
- the stonemason removes the whole object when `IsSmall()` is true

`IsSmall()` also checks the masked raw state:

```cpp
(state & RAW_STATE_MASK) == 0
```

So the final transition is not performed by `Hew()`. It is performed by
destroying the granite object.

## Visual Size

Rendering uses `GetVisualSize()`, not `GetSize()`:

```cpp
rawState = state & RAW_STATE_MASK;
visualSize = boosted ? rawState / 2 : rawState;
return min(visualSize, 5);
```

For boosted granite, two durability states usually map to one visual sprite.
That is the reason a boosted rock can yield twice as many stones while still
looking like the original map object.

## Event-Log Size

Environment logging records remaining stones, not raw state and not visual
size.

The conversion is:

```cpp
remainingStones = granite.GetSize() + 1;
```

For initial granite records, `granite_size_before` and `granite_size_after`
are both set to the current remaining-stone count.

For stonemason hew records:

- normal hew logs `N -> N - 1`
- final depletion logs `1 -> 0`

This matches the actual gameplay behavior: a granite object with raw state `0`
is still present and can produce one last stone, but after that work finishes
the object is destroyed.

## Practical Summary

- `state` is serialized storage and may include `BOOSTED_STATE_FLAG`.
- `GetSize()` returns masked raw durability only.
- remaining stones are `GetSize() + 1` while the object exists.
- `Hew()` decrements raw durability and preserves the boosted flag.
- raw state `0` is the final removable stone, not an empty rock.
- visual size is derived separately and may stay unchanged across a hew.
