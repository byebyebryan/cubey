# Ocean

`ocean` is the active Cubey ocean renderer. It uses the GodotOceanWaves-derived
spectrum/FFT/unpack core as a guardrail, then layers configurable cascade slots,
atmosphere integration, terrain-field descriptors, expanded foam diagnostics,
and debug views behind explicit feature-isolation controls.

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
./build/dev/projects/ocean/ocean --debug-view foam-core
./build/dev/projects/ocean/ocean --debug-view foam-candidate
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
./build/dev/projects/ocean/ocean --debug-view displacement --ocean-cascade 0
./build/dev/projects/ocean/ocean --no-ocean-spectral-domains
./build/dev/projects/ocean/ocean --ocean-terrain-fields
```

The GUI panel also includes cascade isolation, camera presets including a wide
repeat-inspection camera plus mid/high large-scale inspection views, a paused
single-frame step button, a portable wire overlay, a 50 m sea-level-centered
size reference pillar in final view with 1 m, 5 m, and 10 m markers plus a
basic direct-light ocean shadow, experimental heightfield wave self-shadowing,
feature-isolation controls, and LOD breakdown tables for checking clipmap
coverage, patch counts, triangle load, cascade distance fades, and mesh-cell
support while tuning the mesh.
Headless captures can use
`--ocean-cascade all|0|1|2|3|4`,
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

Cascades are now treated as regular slots. The default `Core` preset enables
only C0 and C1, which are the reference-derived wave pair carrying the current
shape and whitecaps. C2, C3, and C4 stay available as opt-in candidate slots for
large-scale breakup or fine detail experiments, but they are not part of the
default cost. Per-slot `Domain min waves` controls decide whether spectral
domain filtering cuts a slot down to a wavelength band; C0/C1 default to the
full spectrum so the primary whitecap carrier stays coherent. Foam is stored
separately from normal data as persistent history, current Jacobian breaking
source, determinant, and compression diagnostic channels. Final whitecap
coverage is driven from the total enabled-slot foam signal, while
`foam-core`/`foam-candidate`/`foam-detail` remain debug buckets. Compression is
currently a diagnostic signal only. This is still not a localized wind or
weather simulation.

Feature isolation controls expose global shape strength, global foam strength,
foam history, shape and detail anti-repeat, split atmosphere material influence,
shape/normal/foam fade distances, and terrain foam strength. Shape LOD now
combines distance fade with mesh-cell support, so coarse clipmap rings stop
carrying displacement detail that the current mesh cannot represent while
normal/foam detail can continue as shading-only contribution. The `Active
cascade work` toggles are stronger than contribution sliders: they skip disabled
cascade spectrum, modulation, FFT, and unpack dispatches, then hide those
cascades from the surface shader. Use `All slots`, `Core`, and `Cheap` to check
which slots and material additions are worth their GPU cost.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
