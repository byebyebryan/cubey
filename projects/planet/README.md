# Planet

`planet` is Cubey's orbital-only planet product. It renders a deterministic
Earth-like globe for whole-disk and high-orbit views; it is not a terrain
streamer, a surface renderer, or an ocean/coast integration project.

The surface is a cached six-face direction-domain RGBA product:

- `R`: normalized land elevation
- `G`: land mask
- `B`: polar and highland ice mask
- `A`: macro roughness

The fixed sphere samples those fields with direct lighting, ocean glint, a
soft terminator, a restrained cloud veil, and the shared atmosphere/night-sky
background. It has no displacement, cube-sphere patch selection, or LOD.

## Run

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --planet-view terminator --planet-disk-coverage 0.48
./build/dev/projects/planet/planet --headless --planet-view crescent \
  --planet-terrain-seed 9012 --width 1600 --height 900 --output /tmp/planet.png
```

`--planet-view` accepts `lit`, `terminator`, `crescent`, and `night`.
`--planet-surface-quality draft` selects a 256-pixel cubemap; `standard` is
512 pixels. `--planet-terrain-seed` controls the deterministic source, and
`--planet-disk-coverage` keeps the visible disk in the V1 envelope of
15-70 percent of viewport height. `--planet-camera-mode surface` is rejected
deliberately.

Use `--debug-view land`, `elevation`, `ice`, `roughness`, or `albedo` to inspect
source channels without material or lighting.

## Review

```sh
projects/planet/capture_orbital_review.sh
```

The script writes the lit, phase, and source-field review pack to
`outputs/planet/orbital-v1`. The procedural cache remains local at
`cache/procedural/v1` and is not committed.

The full boundary, reference provenance, and explicitly deferred scope live in
[the planet rendering architecture](../../docs/architecture/planet-rendering.md).
The frozen implementation remains at
[`projects/planet_legacy`](../planet_legacy/README.md).
