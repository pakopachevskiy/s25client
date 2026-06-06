# Figure Rendering

This note describes how map figures are drawn in the game view and where their
sprite frames come from. It complements
`docs/architecture/resource-loading.md`, which covers the general archive and
override pipeline.

## Overview

`GameWorldView` decides when a figure is drawn, but the figure classes decide
which sprite frame to use. The main draw-order entry points are in
`libs/s25main/world/GameWorldView.cpp`:

- `Draw(...)` scans visible map nodes row by row.
- `DrawMovingFiguresFromBelow(...)` collects figures walking up into the
  current row so they can be drawn between terrain rows.
- `DrawFigures(...)` draws stationary figures immediately and delays moving
  figures that should appear between rows.

The delayed list is drawn after the row scan step. This keeps walking figures
layered correctly against terrain, roads, objects, and other figures. Ships are
also delayed through the same between-row path even when they are not moving.

## Human Figure Sprites

Most human figure rendering is selected in `libs/s25main/figures/noFigure.cpp`
and derived figure classes. The main source archives are loaded at game start by
`Loader::LoadFilesAtGame(...)` in `libs/s25main/Loader.cpp`.

| Resource ID | Runtime file | Main use |
| --- | --- | --- |
| `jobs` | `<RTTR_GAME>/DATA/BOBS/JOBS.BOB` | Walking bodies and job overlays for most workers and soldiers |
| `carrier` | `<RTTR_GAME>/DATA/BOBS/CARRIER.BOB` | Carrier bodies and ware overlays while carrying wares |
| `rom_bobs` | `<RTTR_GAME>/DATA/CBOB/ROM_BOBS.LST` | Work, idle, combat, and other direct animation frames |
| `boat` | `<RTTR_GAME>/DATA/BOBS/BOAT.LST` | Boat-carrier sprites while on water |
| `boot_z` | `<RTTR_GAME>/DATA/BOOT_Z.LST` | Ships and ship-related sprites |
| `charburner_bobs` | `RTTR/assets/base/charburner_bobs.lst` | Charburner addon worker and object frames |
| `wine_bobs` | `RTTR/assets/base/wine_bobs.lst` | Wine addon workers, grapes, wine, and temple servant frames |

Walking frames use six image directions and eight animation steps. The helper
`noFigure::calcWalkFrameIndex(...)` converts a start index, direction, and
animation step into a flat archive index for simple LST-backed animations.
BOB-backed animations use `glArchivItem_Bob::Draw(...)` or loader-built sprite
caches so that a visible frame can be composed from body, overlay, and shadow
layers.

## Carriers And Workers

Carrier-like drawing is split by state:

- Normal walking without ware uses `Loader::getCarrierBobSprite(...)`, which
  reads the helper body/overlay from `JOBS.BOB`.
- Walking with ware uses `Loader::getCarrierSprite(...)`, which composes body
  frames from `CARRIER.BOB` and ware overlays from `CARRIER.BOB` or
  `wine_bobs`.
- Boat carriers on water use `Loader::getBoatCarrierSprite(...)`, which reads
  body frames from `BOAT.LST`.
- Waiting carrier idle animations can draw direct frames from `ROM_BOBS.LST`
  through `Loader::GetCarrierAnimationImage(...)`.

Building workers that return home with goods use
`nofBuildingWorker::DrawWalkingWithWare(...)`. Carry IDs below
`CARRY_ID_CARRIER_OFFSET` are interpreted as `JOBS.BOB` overlays; IDs at or
above that offset are interpreted as carrier ware graphics.

## Map-GFX Components

Some visible figure pieces do not come from the human BOB archives. They come
from the active landscape graphics archive selected by the world description:

- greenland: `<RTTR_GAME>/DATA/MAP_0_Z.LST`
- wasteland: `<RTTR_GAME>/DATA/MAP_1_Z.LST`
- winterworld: `<RTTR_GAME>/DATA/MAP_2_Z.LST`

`Loader::GetMapImage(...)` and `Loader::GetMapTexture(...)` read from this
active archive. It supplies:

- figure walking shadows, including the common shadow range beginning at index
  `900`
- donkey sprites and donkey shadows
- animal sprites and animal shadows
- ware-on-donkey and ware-on-boat overlays
- job, ware, and map-object icons used by nearby UI and overlay code

Animals are drawn through `noAnimal::Draw(...)`, but their frames are cached in
`Loader::fillCaches(...)` from the active map graphics archive rather than from
`JOBS.BOB` or `ROM_BOBS.LST`.

## Runtime Sprite Caches

The loader packs many multi-layer figure sprites into `glSmartBitmap` caches
before gameplay rendering uses them:

- `bob_jobs_cache[nation][job][dir][ani_step]` for most walking jobs
- `fat_carrier_cache[nation][dir][ani_step]` for fat helper/carrier bodies
- `carrier_cache[fat][ware][dir][ani_step]` for normal ware-carrying carriers
- `african_carrier_cache[fat][ware][dir][ani_step]` for African carrier
  variants
- `boat_cache[dir][ani_step]` and `african_boat_cache[dir][ani_step]` for
  boat carriers
- `fight_cache[nation][rank][dir]` for soldier combat animations from
  `ROM_BOBS.LST`
- animal and donkey caches from the active map graphics archive

African carrier skin recoloring is applied while building these caches. Direct
carrier idle frames from `ROM_BOBS.LST` are handled lazily by
`Loader::GetCarrierAnimationImage(...)`.

## Viewing Sprite Archives

Use the vendored `libsiedler2` `lstpacker` tool to unpack `.LST` and `.BOB`
archives into viewable BMP files. Build it with the examples enabled:

```sh
cmake -S . -B build -DRTTR_BUILD_LIBSIEDLER2_EXAMPLES=ON
cmake --build build --target lstpacker
```

Then unpack an archive with the original Settlers II palette:

```sh
build/external/libsiedler2/examples/lstpacker/lstpacker \
  -p /path/to/S2/GFX/PALETTE/PAL5.BBM \
  /path/to/S2/DATA/CBOB/ROM_BOBS.LST
```

For BOB archives:

```sh
build/external/libsiedler2/examples/lstpacker/lstpacker \
  -p /path/to/S2/GFX/PALETTE/PAL5.BBM \
  /path/to/S2/DATA/BOBS/JOBS.BOB
```

`lstpacker` creates an output directory named after the archive stem. Bitmap
entries are written as BMP files with offset metadata in the filename, and BOB
archives also emit `mapping.links`, which describes how body and overlay images
are connected for each logical BOB sprite.

To view the unpacked images as PNGs, convert the exported BMP files after
unpacking. For example, with ImageMagick:

```sh
magick mogrify -format png JOBS/*.bmp
```

or for a single file:

```sh
magick JOBS/0.player.nx0.ny0.bmp JOBS/0.png
```

This converts the raw component images. A BOB figure frame is often composed
from separate body, overlay, and shadow entries using `mapping.links`, so the
converted PNGs are not always the final in-game composed frame. Exporting fully
composed figure frames requires a renderer/exporter that applies the BOB
mapping before writing the PNG.

The checked-in RTTR addon archives can be unpacked the same way:

```sh
build/external/libsiedler2/examples/lstpacker/lstpacker \
  -p /path/to/S2/GFX/PALETTE/PAL5.BBM \
  data/RTTR/assets/base/charburner_bobs.lst \
  data/RTTR/assets/base/wine_bobs.lst
```

Do not commit extracted original Settlers II assets. They are local inspection
artifacts only.
