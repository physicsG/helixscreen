# AMS Logos

Logos for multi-material system identification. Most are white marks on a transparent background, which the UI tints to the text color; a full-color logo (OpenAMS) is drawn as is. Which is which is the `color` flag of `AmsState::get_logo()` (`src/printer/ams_state.cpp#AmsState::get_logo`).

## Regenerating PNGs from SVGs

See [BUILD_SYSTEM.md - SVG to PNG Conversion](../../../docs/BUILD_SYSTEM.md#svg-to-png-conversion) for details on using `rsvg-convert`.

```bash
# Regenerate all logos at 64x64:
for svg in *.svg; do
  rsvg-convert "$svg" -w 64 -h 64 -o "${svg%.svg}_64.png"
done
```

## Supported Systems

| File | System |
|------|--------|
| box_turtle | AFC / Box Turtle |
| ercf | ERCF (Enraged Rabbit) |
| 3ms | 3MS |
| tradrack | Tradrack |
| mmx | MMX |
| night_owl | Night Owl |
| quattro_box | Quattro Box |
| qidi_box | QIDI Box |
| btt_vivid | BTT ViViD |
| kms | KMS |
| openams | OpenAMS, full color (MIT, Copyright (c) 2025 OpenAMS; notice in [LICENSE-openams.md](LICENSE-openams.md)). `openams.svg` is `tools/docs/logo/out/openams-logo.svg` of the OpenAMS project (the light set, which also reads on dark backgrounds); `openams_64.png` is its `rsvg-convert -w 64 -h 64` render |
