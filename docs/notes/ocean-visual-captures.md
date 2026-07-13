# Ocean Visual Captures

Ocean captures are review evidence, not golden-image tests. The canonical
harness keeps sea state, camera scale, environment lighting, and diagnostics in
one reproducible matrix.

## Canonical Review

```sh
MOTION=0 projects/ocean/capture_ocean_review.sh outputs/ocean-review
```

The default 1280x720 pack records the source commit and generates:

- Calm, Windy, and Stormy clear-noon views at low, mid, and high cameras;
- Windy cloudy-noon views at close, mid, high, and wide cameras;
- fixed dawn, dusk, and night lighting checks;
- warmed foam for all three sea states;
- focused specular, reflection, cloud-shadow, LOD, and far-field diagnostics;
- `manifest.tsv`, `index.md`, and `contact-sheet.png` when ImageMagick exists.

Use motion when a change affects wave evolution, foam history, cloud motion,
reflection stability, or time-of-day transitions:

```sh
MOTION=1 projects/ocean/capture_ocean_review.sh outputs/ocean-review-motion
```

Useful harness overrides:

```sh
WIDTH=1920 HEIGHT=1080 FRAMES=120 FOAM_FRAMES=180 \
  MAP_SIZE=512 CLOUD_QUALITY=full MOTION=0 \
  projects/ocean/capture_ocean_review.sh outputs/ocean-review-full
```

## Review Contract

Check the matrix in this order:

1. Low and close views retain coherent crest shapes instead of rounded bulges.
2. Whitecaps follow crest regions and remain distinct across sea states.
3. Mid/high views become reflection-led without turning featureless or exposing
   FFT tile repetition.
4. Sun glitter stays in a plausible reflected-light corridor.
5. Cloud reflection has no planar coverage hole or false twilight colors.
6. Cloud shadows modulate direct light without looking like a separate texture.
7. Dawn, dusk, and night keep water, foam, and sky in the same exposure regime.
8. LOD diagnostics agree with visible displacement and submitted triangle load.

For motion, look specifically for reflection flashes, foam popping, moving tile
boundaries, time-of-day discontinuities, and noise that is hidden in stills.

## Focused Commands

Use a focused command only after the canonical matrix identifies a failure:

```sh
./build/dev/projects/ocean/ocean --headless --frames 120 \
  --width 1280 --height 720 --ocean-sea-state windy \
  --ocean-camera-preset high --debug-view energy-lod \
  --output outputs/ocean-energy-lod.png

./build/dev/projects/ocean/ocean --headless --frames 120 \
  --width 1280 --height 720 --ocean-sea-state windy \
  --ocean-camera-preset mid --debug-view cloud-reflection-validity \
  --output outputs/ocean-reflection-validity.png

./build/dev/projects/ocean/ocean --headless --frames 180 \
  --width 1280 --height 720 --ocean-sea-state stormy \
  --ocean-camera-preset low --no-clouds --debug-view foam \
  --output outputs/ocean-stormy-foam.png
```

Cascade selection is diagnostic rather than a separate feature mode:

```sh
./build/dev/projects/ocean/ocean --headless --frames 120 \
  --debug-view displacement --ocean-cascade 0 \
  --output outputs/ocean-c0-displacement.png
```

## Performance Review

Cloud reflection source cost has a separate harness:

```sh
projects/ocean/profile_cloud_reflections.sh outputs/ocean-reflection-profile
```

It compares cached and planar sources and records component GPU spans. Do not
infer runtime cost from capture-video wall time; use the generated profile
summary and keep resolution, ocean map, cloud quality, warmup, and frame count
fixed across comparisons.
