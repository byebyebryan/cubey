# Terrain Water

Terrain-water simulation, rendering and integration. The current featured scene
uses rain on unmodified Terrain Diffusion hills: streams form downhill, join,
collect in basins, and recede when supply stops. Rain is a scenario, not the
project identity. The retained builtin shallow-water experiments remain in
[Fluid 2.5D](../fluid/fluid_25d/README.md).

https://github.com/user-attachments/assets/453158a4-156c-4031-85ec-f6481780ea08

[Published sunset highlight](../../docs/media/showcase/fluid-25d.mp4) ·
[1080p master](../../docs/media/showcase/fluid-25d-master-1080p.mp4) ·
[Frozen recipe](../../docs/media/showcase/fluid-25d-recipe.json) ·
[Publication receipt](../../docs/media/showcase/fluid-25d-publication.json)

The media keeps its historical `fluid-25d` filenames and source commands. This
is the existing owner-selected Joining valleys film, not a new capture or a live
performance claim: approximately six physical hours become 5.6 active seconds
in an eight-second edit; 72 saved water states are held between saves.

## Build and explicit setup

```sh
rtk proxy cmake --preset release
rtk proxy cmake --build --preset release --target cubey_project_terrain_water -j 6
```

The live application runs SynxFlow in a separate CUDA worker. Cubey is a Vulkan
client using bounded CPU-staged depth/momentum publications, not CUDA–Vulkan
memory interop or a solver port. The worker/build slice is separately marked
GPL-3.0-only; it is not linked into the MIT renderer. See the retained
[dependency setup and upstream credits](external_synxflow/README.md).

Prepared terrain/climate cases and a qualified worker environment are required
for live use. A fresh checkout alone is not a complete runnable terrain demo.
Normal build/startup does not install packages, download models, generate
terrain or build CUDA. Follow [explicit preparation](demo_setup_v1.md).
The native extension must retain its qualified hash, including with path overrides.

## Managed live demo

```sh
# Admission; no worker or GUI starts. Add --check-runtime for optional ABI/import checks.
rtk proxy python3 projects/terrain_water/run_terrain_rain_demo.py \
  --study outputs/fluid/demo-local --print-command

# Same prepared inputs, local GUI.
rtk proxy python3 projects/terrain_water/run_terrain_rain_demo.py \
  --study outputs/fluid/demo-local
```

Existing maintainer evidence may be selected explicitly with
`--study outputs/fluid/site-expansion-20261011-jtIA3s`; that dated directory is
not a repository dependency. `--native-python` and `--extension` select existing
qualified artifacts without installing or rebuilding anything. An interpreter's
venv path is retained rather than resolving its symlink to base Python.

`--print-command --check-runtime` additionally checks the qualified environment
and native library imports, using temporary library caches. It is live-preflight
only, never a solver dispatch or new-build qualification. Replay never imports
the worker. The ordinary `--print-command` remains identity/admission-only.

Default prepared site: Mountain / 1024² / 16 m / 16.384 km square, Scenic Macro,
matched B-spline 2x ground/water, correlated terrain, scene-fitted shadows, rough
water, visible rain and dots/trails. Bank noise remains optional/off. Only the
full-resolution simulation patch is rendered. Six sites and five power-of-two
grid presets are offered when their exact prepared cases are available.

Startup is dry and paused. Start/Space on the qualified Mountain bed guides
512 mm/h rain at requested 60x, turns input off at two simulated hours and ends
its explanation at six hours **without stopping the solver**. Manual retains
ordinary continuous operation. Other sites/grids do not silently inherit that
qualified guide. Manual controls/camera input exit guidance; Apply & restart
replaces the complete session, dry/paused, not existing water on a new grid.

Rain supplies the whole patch; water can leave the open perimeter. Rain Off
stops new input, not runoff. Basin growth may continue briefly as upstream water
arrives. Intensity, requested physical pace, renderer FPS, playback and solar
time are separate. Foam, rain and dots are visual cues, not conserved particles.
This is exaggerated demonstration rainfall on generated terrain, not calibrated
hydrology. Source elevation is 30 m; finer cells interpolate it.

For automation use `--headless --run-seconds 30 --out outputs/fluid/FRESH`.
Without a control script this remains dry/paused. The tracked lifecycle fixture
is [headless-controls.json](tests/fixtures/headless-controls.json);
it starts, changes rain, pauses and resets, not a performance qualification.

## Replay and builtin alternatives

```sh
rtk proxy python3 projects/terrain_water/run_terrain_rain_demo.py replay \
  --study outputs/fluid/demo-local --recording /path/to/recording.json
```

Replay needs prepared terrain and recorded fields but no CUDA computation or
worker environment. It initially holds the final saved state. R rewinds; viewing
controls do not change recorded hydraulics. Native expert entry points retain
the established `--fluid25d-*` flags:

```sh
rtk proxy build/release/projects/terrain_water/terrain_water --help
rtk proxy build/release/projects/terrain_water/terrain_water \
  --fluid25d-backend recording --fluid25d-recording /path/to/recording.json
```

The managed launcher owns the accepted appearance preset; direct native commands
retain compatible low-level defaults. No-input native startup reports the needed
producer instead of silently starting another simulation. An explicit
`--fluid25d-backend builtin` is retained as a diagnostic alternative, not an
equivalent mountain-flow backup. Prefer the dedicated `fluid_25d` executable for
builtin experiments. No silent fallback or water-state transfer occurs.

## Ownership and integration

The [integration inventory](integration.md) records actual consumers, deliberate
exclusions, compatibility paths and the builtin backup qualification boundary.

- **Terrain Water:** this frontend and managed launcher/supervisor; preferred
  external terrain experiment and recorded presentation.
- **Fluid 2.5D:** builtin virtual-pipes/finite-volume experiments and numerical
  oracles. Existing external/replay commands are labelled compatibility entries.
- **Shared shallow-water components:** retained under `projects/fluid/sim/fluid_25d`
  and compiled from one source list for both frontends. Protocols, flags and
  namespaces are not gratuitously renamed. No mathematical implementation is forked.
- **Shared Terrain foundation:** asset admission, captured-water runtime and
  paired terrain/water renderer under `include/cubey`, `src/cubey` and shared shaders.
  [Terrain](../terrain/README.md) consumes saved landscapes without a running
  solver; [glTF](../gltf_viewer/README.md#showcase-highlight) is an accepted consumer.

Maintained setup/runtime helpers live in [tools](tools/README.md); worker/build
source and licensing live in [external_synxflow](external_synxflow/README.md).
Historical notes/runners are [indexed under studies](../../studies/fluid_25d/README.md),
not imported by normal startup. The qualified ignored native binary stays at its
original build path/RPATH. The old managed launcher forwards here; it does not
run a second implementation. Water 3D and Pyro adoption, solver
research, Terrain default promotion and new media publication are out of scope.

See the current [behavior reference](current_reference.md),
[decision history](../../studies/fluid_25d/notes/development_map.md) and
[showcase reproduction](../../studies/fluid_25d/notes/presentation_v1.md).
