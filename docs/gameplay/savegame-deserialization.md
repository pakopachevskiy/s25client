# Savegame Deserialization

This note describes the savegame load path used by
`extras/data-extractor/SnapshotLoader.cpp` and the normal game startup path.
The outer `.sav` file is handled by `Savegame`; the live world snapshot inside
it is handled by `SerializedGameData`.

## Data Extractor Entry Point

`Snapshot::GetActivePlayer()` is a minimal savegame consumer:

1. Open a `Savegame` and call `Load(save_path, SaveGameDataToLoad::All)`.
2. Copy every stored `BasePlayerInfo` into a `PlayerInfo` vector.
3. Construct a fresh `Game` with default global settings and those players.
4. Call `savegame.sgd.ReadSnapshot(*game, GameClient::inst())`.
5. Return one `PlayerSnapshot` for every used player in the loaded world,
   carrying a shared pointer to the hydrated `Game`, the `GamePlayer*`, and the
   stored start gameframe.

The data extractor stops at this hydrated snapshot and reads player statistics
from it. The normal client and headless AI paths perform the same
`ReadSnapshot()` step, then call `world_.InitAfterLoad()` before running the
simulation.

## Outer Savegame File

`Savegame` inherits the common `SavedFile` framing used by savegames and
replays. A current savegame uses signature `RTTRSV` and savegame format version
`4`. Loading rejects files with a different signature or version before any
gameplay data is read.

The current save path writes data in this order:

1. File header: signature and savegame format version.
2. Extended header: RTTR revision, save timestamp, map name, saved player-name
   list, and the savegame start gameframe (`start_gf`).
3. Player data block: a length-prefixed `Serializer` buffer containing the
   player slot count and light `BasePlayerInfo` data for each slot.
4. Global game settings block: another length-prefixed `Serializer` buffer
   containing `GlobalGameSettings`.
5. Game snapshot data: the raw `SerializedGameData` buffer, usually compressed.

`SaveGameDataToLoad` controls how far the reader proceeds:

- `Header` reads only the file and extended headers.
- `HeaderAndSettings` also reads player data and global settings.
- `All` additionally reads and decompresses the game snapshot.

`SnapshotLoader` uses `All` because it needs the actual world and player state,
not just lobby metadata.

## Game Data Compression

The final savegame section is the `SerializedGameData` buffer. New savegames
write a compatibility flag first:

1. `1` as an unsigned int, meaning the following payload is compressed.
2. Uncompressed byte length.
3. Compressed byte length.
4. Compressed bytes.

On load, `Savegame::ReadGameData()` decompresses those bytes into a temporary
buffer, clears the existing `SerializedGameData`, and pushes the raw bytes into
it. Old savegames did not have the flag; for those, the first unsigned int is
treated as the raw data size and the following bytes are copied directly.

In debug builds the decompressed raw snapshot is also written to
`rttrGameData.raw` in the system temporary directory, which is useful when
inspecting a problematic save.

## Snapshot Buffer Format

`SerializedGameData::ReadSnapshot()` consumes the decompressed buffer in the
same order used by `MakeSnapshot()`:

1. `Prepare(true)` reads the four-byte marker `VER\0` and the game-data
   version. The current game-data version is tracked separately from the outer
   savegame format and is currently `16`.
2. Read the expected live `GameObject` count. This is used later for consistency
   checks.
3. Deserialize the world with `MapSerializer::Deserialize()`.
4. Deserialize the event manager.
5. If the objective is `EconomyMode`, deserialize the economy-mode handler
   object.
6. Deserialize every `GamePlayer` in world player order.
7. Verify that the number of loaded events and objects matches the expected
   counts, then perform a final flag-worker ownership sanity check.

The order is strict. There is no field table or schema in the file; each class
must read exactly the same sequence that its serializer wrote.

## World Deserialization

`MapSerializer::Deserialize()` rebuilds the world before player data is read:

1. Load game-data descriptions from Lua via `GameDataLoader`. These
   descriptions are needed to interpret landscape, terrain, and object data.
2. Read the map size and landscape. Older game-data versions store a legacy
   graphics-set id; version `3` and newer store the landscape name.
3. Initialize the world and reset the `GameObject` id counter from the snapshot.
4. Iterate every map node and let `MapNode::Deserialize()` restore terrain,
   ownership, roads, objects, visibility memories, and per-player state.
5. Restore catapult stones, sea metadata, harbor positions, and harbor building
   sites.
6. Restore Lua state if the save contains a script. The script text is loaded,
   the script version is checked, and the serialized Lua save state is passed
   back through the Lua interface. Start and end magic values guard the Lua
   payload.
7. Recreate trade graphs from the restored world data.

After this step the static world, node objects, harbor network, optional Lua
state, and object id space are present, but scheduled events and player-owned
registries still need to be loaded.

## Object And Event References

Savegame data contains many pointers between objects, events, buildings,
figures, roads, wares, and fog-of-war memories. `SerializedGameData` preserves
those relationships by serializing ids before payloads:

- A null object pointer is stored as object id `0`.
- A non-null object starts with its object id. If that id was already loaded,
  the existing pointer is returned and no payload follows.
- For a first occurrence, the stream contains the `GO_Type` unless the caller
  already knows the type from context. `Create_GameObject()` dispatches that
  type to the matching constructor, such as `nobHQ`, `nofCarrier`, `noFlag`,
  `RoadSegment`, `Ware`, or `noShip`.
- Each object's deserializing constructor reads its own fields. The serialized
  `GameObject` base constructor registers the instance with
  `SerializedGameData::AddObject()` so future references to the same id resolve
  to the same pointer.
- A safety code follows the payload. It combines the object id and type, and
  load fails if the code does not match the object that was just built.

Events follow the same pattern with event instance ids. A null event is `0`; a
new event is constructed from `GameEvent(*this, instanceId)`, registered through
`AddEvent()`, and checked with an event safety code.

Fog-of-war objects are simpler. They store only a fog object type byte followed
by that fog object's payload, or `FoW_Type::Nothing` for an empty memory.

## Versioning Rules

There are two version numbers:

- `Savegame::GetVersion()` is the outer file format gate. If this changes, old
  save files are rejected unless conversion code is added at the `Savegame`
  level.
- `currentGameDataVersion` in `SerializedGameData.cpp` is the inner snapshot
  format. Compatible gameplay-data changes increment this value and keep
  conditional read code for older versions.

The comment above `currentGameDataVersion` is the active changelog for snapshot
schema changes. When a change is too large to support conditionally, the outer
savegame version should be increased and the inner game-data version reset.

## Failure Modes

Deserialization fails by returning `false` from `Savegame::Load()` or by
throwing `SerializedGameData::Error` during snapshot hydration. Common reasons
include:

- wrong file signature or savegame version,
- malformed or truncated length-prefixed blocks,
- invalid compressed data,
- missing or invalid `VER\0` snapshot marker,
- unknown landscape, game object type, or enum value,
- invalid Lua payload markers or Lua script/load callback failure,
- object or event count mismatches,
- invalid safety code after constructing an object or event.

`SnapshotLoader` catches standard exceptions, logs the failing path, and returns
an empty snapshot list so the data extractor can skip that file.
