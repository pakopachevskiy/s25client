# Quantity Extractor

`quantity-extractor` reads an RTTR `.sav` file, restores the saved game state,
and writes binary protobuf snapshots for building-quality and territory
analysis.

```text
quantity-extractor <save-file> [output-dir]
```

If `output-dir` is omitted, the current working directory is used. The output
directory is created when it does not exist.

## Outputs

The tool writes:

- `building_locations.pb` (`BuildingLocationsFile`)
- `road_locations.pb` (`RoadLocationsFile`)
- `environment_snapshot.pb` (`EnvironmentSnapshotFile`)
- `building_quality_snapshot.pb` (`BuildingQualitySnapshot`)

All files set `schema_version` to `quantity-extractor/v1` and `gameframe` to
the savegame start frame. Road, environment, and `BuildingQualitySnapshot`
outputs also include map width and height.

Player IDs use the engine convention: zero-based player slots.

## Notes

The savegame loader uses the saved `GlobalGameSettings`, restores the snapshot
through a local `ILocalGameState`, and calls `GameWorld::InitAfterLoad()` before
extracting data.

Road `constructed_gameframe`, tree `created_gameframe`, and granite
creation/update frames are currently exported as `0` because the engine does
not expose those values for snapshot extraction.

## Building Quality Snapshot

`building_quality_snapshot.pb` stores a `BuildingQualitySnapshot`. It contains
one row-major record per map node with terrain BQ inputs, altitude, owner,
harbor id, stored point roads, blocker identity, raw node BQ, and adjusted BQ
values by zero-based engine player slot.

See [building-quality-snapshot.md](building-quality-snapshot.md) for how to use
the snapshot to calculate plot building quality.
