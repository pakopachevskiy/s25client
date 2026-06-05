# Carrier Skin Recoloring

This note documents how carrier skin recoloring is applied during sprite cache
construction.

## Purpose

Original carrier sprites use the same light skin tones for every nation. RTTR
keeps the original carrier assets, but African carriers are recolored at load
time so their exposed skin appears darker while preserving the original frame
shading.

The target African base color is:

- `#5B2F13`

The known original carrier skin colors are:

- `#DBC797`
- `#BFA373`
- `#A78353`
- `#A49571`

## Recolor Helper

The pixel-level recolor logic lives in
`libs/s25main/ogl/CarrierSkinRecolor.cpp`.

`carrierSkinRecolor::createAfricanCarrierSkin(...)`:

1. clones a `glArchivItem_Bitmap_Player`
2. converts the clone to `TextureFormat::BGRA`
3. skips transparent pixels
4. skips player-color pixels
5. replaces pixels near the known carrier skin colors
6. scales the target color by source luminance so light and dark animation
   frames keep their original shading

The helper returns an owned bitmap clone. `glSmartBitmap` has an owning
`add(std::unique_ptr<ArchivItem_Bitmap_Player>)` overload so recolored clones
can safely live inside cached composed sprites.

## Loader Cache Integration

Carrier recoloring is done in `Loader::fillCaches()`, before sprites are packed
into textures.

The loader keeps the normal non-African carrier caches unchanged and builds
separate African variants where carrier assets were previously shared:

- `carrier_cache` for normal ware-carrying carriers
- `african_carrier_cache` for African ware-carrying carriers
- `boat_cache` for normal boat-carrier sprites
- `african_boat_cache` for African boat-carrier sprites

Nation-specific no-ware carrier sprites are already cached per nation in:

- `bob_jobs_cache[nation][Job::Helper]`
- `fat_carrier_cache[nation]`

For `Nation::Africans`, the loader recolors the body and relevant overlay
layers before adding them to those caches.

Idle carrier animations are a special case. `nofCarrier` plays some animation
frames directly from `rom_bobs`. Those frames go through
`Loader::GetCarrierAnimationImage(nation, frameId)`, which returns the original
frame for non-African nations and lazily creates a recolored clone for African
carriers.

## Draw-Site Selection

Draw code should pass the owning nation when selecting carrier-like sprites:

- `noFigure::DrawWalkingCarrier(...)` uses the owner's nation for walking
  carriers and warehouse worker carriers.
- `nofCarrier::Draw(...)` uses the owner's nation for waiting carriers,
  ware-carrying carriers, boat carriers, and idle carrier animations.
- `nofWellguy::DrawWorking(...)` uses the workplace nation for carrier-style
  bucket walking frames.

Donkeys are not recolored because their draw path renders the donkey and ware,
not a human carrier body.

## Testing

Pixel-level behavior is covered in `tests/s25Main/UI/testSmartBitmap.cpp`.

The tests verify that:

- all known original carrier skin colors are recognized
- `#BFA373` maps exactly to `#5B2F13`
- lighter and darker skin source colors stay lighter and darker after recolor
- unrelated colors are unchanged
- transparent pixels are unchanged
- player-color pixels are unchanged
- paletted source bitmaps are safely converted to BGRA before recoloring

Useful focused validation commands:

```sh
cmake --build cmake-build-debug --target s25client Test_UI
ctest --test-dir cmake-build-debug -R UI --output-on-failure
```

## Practical Summary

- Recolor carrier skin in loader caches, not during every draw call.
- Add new source skin tones to `CarrierSkinRecolor.cpp`.
- Recolor both body and overlay layers when carrier skin can appear in either.
- Keep player-color pixels untouched so player clothing and nation color
  tinting continue to work normally.
