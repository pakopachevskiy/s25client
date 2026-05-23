# Quantity Extractor

`quantity-extractor` reads an RTTR `.sav` file, restores the saved game state,
and writes protobuf snapshots for building-quality and player-territory
analysis.

```text
quantity-extractor <save-file> [output-dir]
```

If `output-dir` is omitted, files are written to the current working directory.
The directory is created when it does not exist.

## Output Files

The tool writes:

- `building_locations.pb`: `BuildingLocationsFile`
- `road_locations.pb`: `RoadLocationsFile`
- `environment_snapshot.pb`: `EnvironmentSnapshotFile`
- `building_quality_snapshot.pb`: `BuildingQualitySnapshot`
- `players.json`: player metadata

All protobuf files use `schema_version = "quantity-extractor/v1"` and
`gameframe` from the savegame start frame. Road, environment, and
building-quality input snapshots also include `map_width` and `map_height`.

## Player IDs

Player IDs in `building_locations.pb`, `road_locations.pb`, and `players.json`
are exported as one-based IDs. In `building_quality_snapshot.pb`, `owner_id`
uses the same convention, with `0` reserved for neutral map nodes.
`adjusted_bq_by_player` remains indexed by zero-based engine player slot.

## Known Zero Fields

Some engine objects do not currently expose provenance for snapshot export:

- road `constructed_gameframe` is exported as `0`;
- tree `created_gameframe` is exported as `0`;
- granite `created_gameframe` and `updated_gameframe` are exported as `0`.

## Environment Snapshot

Trees and granite remain in their dedicated fields. Other node objects with a
blocking manner other than `BlockingManner::None` are exported as
`blocking_objects`, including buildings, construction sites, flags, building
extensions, signs, fires, static objects, and similar blockers.

## Building Quality Inputs

`building_quality_snapshot.pb` contains one row-major record per map node. Each
record includes ownership, altitude, both terrain building-quality inputs,
harbor id, point roads in the engine's three stored directions, object blocking
manner, blocker object identity, raw node `bq`, and adjusted BQ values indexed
by zero-based engine player slot.

These fields are intended as inputs for the building-quality rules documented
in [building-quality.md](../gameplay/building-quality.md).
