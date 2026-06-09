# African Carrier Sprites

This note documents how African carrier sprites are provided.

## Purpose

Original carrier sprites use the same light skin tones for every nation. RTTR
keeps those original carrier assets for non-African nations and ships dedicated
African carrier sprites for African players.

The dedicated sprites were generated from the original carrier frames by
recoloring known carrier skin tones to the African base color `#5B2F13`, while
preserving transparency, player-color pixels, draw offsets, and frame shading.

## Asset Folders

African carrier assets live in:

```text
data/RTTR/assets/nations/Africans/
```

The loader uses these folders as explicit African-only merges with the original
resources:

- `afr_jobs.bob` for no-ware helpers and fat carriers from `jobs.bob`
- `afr_carrier.bob` for ware-carrying carriers from `carrier.bob`
- `afr_boat` for boat-carrier frames from `boat`
- `afr_winebobs` for grape and wine carrier frames from `wine_bobs`
- `afr_rombobs` for idle carrier animation frames from `rom_bobs`

Do not add plain `carrier.bob`, `jobs.bob`, `boat`, `wine_bobs`, or `rom_bobs`
overrides here for this purpose. Those resources are loaded globally, so a plain
nation-folder override would alter the shared archive for every nation in a
mixed game.

## Loader Integration

`Loader::LoadAfricanCarrierResources()` loads each original resource plus its
African sprite folder into a separate owned archive.

`Loader::fillCaches()` then selects those African archives only while building
African carrier caches:

- `bob_jobs_cache[Nation::Africans][Job::Helper]`
- `fat_carrier_cache[Nation::Africans]`
- `african_carrier_cache`
- `african_boat_cache`

Idle carrier animations are returned through
`Loader::GetCarrierAnimationImage(nation, frameId)`, which reads
`afr_rombobs` for African carriers and the original `rom_bobs` for other
nations.

Donkeys are not changed because their draw path renders the donkey and ware,
not a human carrier body.

## Testing

Generated African sprite folders are covered by the resource loader tests. A
focused validation command is:

```sh
ctest --test-dir cmake-build-debug -R resources --output-on-failure
```
