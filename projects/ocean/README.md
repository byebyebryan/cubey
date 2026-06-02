# Ocean

`ocean` is the active Cubey ocean renderer. It is being pulled back toward the
known-good [`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/)
reference core before any of Cubey's preserved experiments are reintroduced.

GodotOceanWaves is MIT licensed; the required notice is kept in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Known-good fallback before the LOD data/domain architecture work:
`ocean-known-good-material-v1` (`400d45c`).

Run a still capture:

```sh
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output /tmp/cubey-ocean.png
```

Useful debug views:

```sh
./build/dev/projects/ocean/ocean --debug-view displacement
./build/dev/projects/ocean/ocean --debug-view normal
./build/dev/projects/ocean/ocean --debug-view foam
./build/dev/projects/ocean/ocean --debug-view foam-source
./build/dev/projects/ocean/ocean --debug-view foam-history
./build/dev/projects/ocean/ocean --debug-view foam-macro
./build/dev/projects/ocean/ocean --debug-view foam-crest
./build/dev/projects/ocean/ocean --debug-view foam-detail
./build/dev/projects/ocean/ocean --debug-view lod
./build/dev/projects/ocean/ocean --debug-view sky-radiance
./build/dev/projects/ocean/ocean --debug-view reflection
./build/dev/projects/ocean/ocean --debug-view direct-light
./build/dev/projects/ocean/ocean --debug-view ambient-light
./build/dev/projects/ocean/ocean --debug-view exposure
./build/dev/projects/ocean/ocean --debug-view terrain-depth
./build/dev/projects/ocean/ocean --debug-view terrain-shore
./build/dev/projects/ocean/ocean --debug-view terrain-slope
./build/dev/projects/ocean/ocean --debug-view lod --ocean-wire-overlay
./build/dev/projects/ocean/ocean --debug-view displacement --ocean-cascade 2
./build/dev/projects/ocean/ocean --no-ocean-spectral-domains
./build/dev/projects/ocean/ocean --ocean-terrain-fields
```

The GUI panel also includes cascade isolation, camera presets including a wide
repeat-inspection camera, a paused single-frame step button, a portable wire
overlay, and an LOD breakdown table for checking clipmap coverage, patch counts,
and triangle load while tuning the mesh. Headless captures can use
`--ocean-cascade all|0|1|2`,
`--ocean-wire-overlay`, `--ocean-wire-opacity 0.0..1.0`,
`--ocean-spectral-domains`, `--no-ocean-spectral-domains`,
`--ocean-terrain-fields`, and `--no-ocean-terrain-fields`.

The visible sky now uses the shared `atmosphere` background path, and water
lighting samples the runtime atmosphere sky/probe data for reflection, ambient
fill, horizon fog, and night-aware foam shading. Ocean still uses its own
non-PBR water material, but the background atlases now come from the shared
generated lunar and night-sky atlas path. A diagnostic terrain-ocean field
texture is bound for terrain depth/shore/slope debug views; enabling
`--ocean-terrain-fields` only proves a small shoreline foam hook and is not yet
full bathymetry, seafloor visibility, or surf-zone rendering.

Cascades now match the three-slot reference layout: `0` is the primary
reference crest, `1` is the secondary reference wave, and `2` is fine
normal/foam detail. Spectral source-domain filtering defaults off so the visible
cascades use the full reference spectrum; turn it on only when inspecting the
old detail banding path. Foam is stored separately from normal data as
persistent history, current Jacobian breaking source, determinant, and
compression diagnostic channels. Compression is currently a diagnostic signal
only. This is still not a localized wind or weather simulation.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
