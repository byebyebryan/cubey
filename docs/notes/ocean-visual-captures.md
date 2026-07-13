# Ocean Visual Capture Recipes

These recipes are for repeatable manual checks while the active ocean renderer
is still being tuned. They are not golden-image tests. The goal is to keep the
far-field repetition, LOD contribution, foam coherence, atmosphere response, and
sun-glitter behavior easy to compare across ocean changes.

All commands write under `outputs/`, which is intentionally ignored by git.

## Sea-State Matrix

Use the checked-in matrix to compare the accepted `Calm`, `Windy`, and `Stormy`
states without changing quality or cascade workload:

```sh
projects/ocean/capture_sea_state_review.sh outputs/ocean-sea-state-review
```

The contact sheet places states in columns and low/mid/high clear views, cloudy
mid view, twilight, displacement, and warmed foam diagnostics in rows. The
default run also records a fixed-lighting motion clip for each state. Review the
low and mid rows for wave/foam separation; the high row is intentionally a
check of the current reflection-dominated far-field handoff rather than a
promise of resolved wave geometry.

## Baseline Matrix

Use this compact still matrix after changing cascade tuning, LOD policy,
surface curvature, atmosphere lighting, or foam shading:

```sh
mkdir -p outputs/ocean-look
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output outputs/ocean-look/final-default.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view lod --output outputs/ocean-look/debug-lod.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view energy-lod --output outputs/ocean-look/debug-energy-lod.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view displacement --output outputs/ocean-look/debug-displacement.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam --output outputs/ocean-look/debug-foam.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-source --output outputs/ocean-look/debug-foam-source.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-history --output outputs/ocean-look/debug-foam-history.png
```

Check that the final view still reads as coherent cresting waves rather than
rounded bulges, the LOD and energy views agree on which cascades are still
resolvable, and the raw foam views do not collapse into regular
texture tiling when the camera is pulled back.

## Cascade Isolation

Use these to verify which enabled slots are carrying shape and whitecaps. The
default renderer should be meaningful with all cascades enabled and with C0/C1
inspected independently. Disabled candidate slots should remain visibly absent
until explicitly enabled in the GUI or through config files.

```sh
mkdir -p outputs/ocean-cascades
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view displacement --ocean-cascade all --output outputs/ocean-cascades/displacement-all.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view displacement --ocean-cascade 0 --output outputs/ocean-cascades/displacement-c0.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view displacement --ocean-cascade 1 --output outputs/ocean-cascades/displacement-c1.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-source --ocean-cascade all --output outputs/ocean-cascades/foam-source-all.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-source --ocean-cascade 0 --output outputs/ocean-cascades/foam-source-c0.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-source --ocean-cascade 1 --output outputs/ocean-cascades/foam-source-c1.png
```

## Far-Field Review

Use these for repeatable zoomed-out stills:

```sh
mkdir -p outputs/ocean-far-field
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset mid --output outputs/ocean-far-field/final-mid.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --output outputs/ocean-far-field/final-high.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --output outputs/ocean-far-field/final-wide.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --debug-view lod --output outputs/ocean-far-field/lod-wide.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --debug-view footprint --output outputs/ocean-far-field/footprint-wide.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --debug-view energy-lod --output outputs/ocean-far-field/energy-lod-wide.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --debug-view far-field --output outputs/ocean-far-field/far-field-wide.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset wide --debug-view foam-source --output outputs/ocean-far-field/foam-source-wide.png
```

Keep the view, time, and feature preset fixed while changing one LOD or cascade
setting at a time. The important failure modes are:

- visible FFT tile repetition in whitecaps or normal detail;
- displacement carried by clipmap rings that are too coarse to represent it;
- foam detail fading differently from wave shape, leaving a flat but tiled
  texture cue;
- filtered foam diagnostics becoming cloudy, noisy, or obviously locked to one
  FFT tile;
- high-frequency normal detail remaining visible as texture noise instead of
  fading into roughness, reflection, and subtle swell hints;
- sun glitter appearing as a uniform sparkle field instead of a reflected-light
  corridor;
- horizon coverage changing because automatic mesh extent silently expanded or
  contracted.

The `Diagnostics` panel should be open during far-field tuning. It exposes the
effective horizon-expanded mesh, near/far cell size, clipmap patch load, and
cascade LOD bands that decide which wave scales are still allowed to contribute.

## Sun Glitter Review

Use these to check the photo-reference target: far water should stay mostly
smooth and reflective, with glitter concentrated under the reflected sun path
rather than spread across the whole ocean:

```sh
mkdir -p outputs/ocean-glitter
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset mid --time-of-day-mode manual --sun-elevation 42 --sun-azimuth -20 --pause-time --output outputs/ocean-glitter/high-sun-mid.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --time-of-day-mode manual --sun-elevation 42 --sun-azimuth -20 --pause-time --output outputs/ocean-glitter/high-sun-high.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --time-of-day-mode manual --sun-elevation 8 --sun-azimuth -20 --pause-time --output outputs/ocean-glitter/low-sun-high.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --time-of-day-mode manual --sun-elevation 42 --sun-azimuth -20 --pause-time --debug-view reflection --output outputs/ocean-glitter/reflection-high-sun-high.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --time-of-day-mode manual --sun-elevation 42 --sun-azimuth -20 --pause-time --debug-view far-field --output outputs/ocean-glitter/far-field-high-sun-high.png
```

## Active Baseline Comparison

The retired `ocean_ref` and `ocean_legacy` projects are available only through
git history. For current tuning, compare repeatable active-ocean captures
before and after a change:

```sh
mkdir -p outputs/ocean-active-baseline
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output outputs/ocean-active-baseline/default.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset mid --output outputs/ocean-active-baseline/mid.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --ocean-camera-preset high --debug-view energy-lod --output outputs/ocean-active-baseline/high-energy-lod.png
```
