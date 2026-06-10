# Ocean Visual Capture Recipes

These recipes are for repeatable manual checks while the active ocean renderer
is still being tuned. They are not golden-image tests. The goal is to keep the
far-field repetition, LOD contribution, foam coherence, and atmosphere response
easy to compare across ocean changes.

All commands write under `outputs/`, which is intentionally ignored by git.

## Baseline Matrix

Use this compact still matrix after changing cascade tuning, LOD policy,
surface curvature, atmosphere lighting, or foam shading:

```sh
mkdir -p outputs/ocean-look
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output outputs/ocean-look/final-default.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view lod --output outputs/ocean-look/debug-lod.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view displacement --output outputs/ocean-look/debug-displacement.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam --output outputs/ocean-look/debug-foam.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-source --output outputs/ocean-look/debug-foam-source.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --debug-view foam-history --output outputs/ocean-look/debug-foam-history.png
```

Check that the final view still reads as coherent cresting waves rather than
rounded bulges, the LOD view shows stable clipmap rings, and the foam views do
not collapse into regular texture tiling when the camera is pulled back.

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

For now, use the GUI camera presets for `Mid`, `High`, and `Wide` far-field
inspection. Keep the view, time, and feature preset fixed while changing one LOD
or cascade setting at a time. The important failure modes are:

- visible FFT tile repetition in whitecaps or normal detail;
- displacement carried by clipmap rings that are too coarse to represent it;
- foam detail fading differently from wave shape, leaving a flat but tiled
  texture cue;
- horizon coverage changing because automatic mesh extent silently expanded or
  contracted.

The `Diagnostics` panel should be open during far-field tuning. It exposes the
effective horizon-expanded mesh, near/far cell size, clipmap patch load, and
cascade LOD bands that decide which wave scales are still allowed to contribute.

## Reference Comparison

Keep `projects/ocean_ref` as the known-good implementation reference. Compare
against it when judging whether a new ocean tuning pass improves crest shape,
foam coherence, or cost:

```sh
./build/dev/projects/ocean_ref/ocean_ref --headless --frames 120 --width 1280 --height 720 --output outputs/ocean-ref-default.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output outputs/ocean-active-default.png
```

The reference remains a visual guardrail, not the target architecture. The
active ocean has additional atmosphere, foam-history, terrain-field,
diagnostic, and feature-isolation paths that make exact cost or pixel matching
the wrong comparison.
