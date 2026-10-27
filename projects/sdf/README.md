# SDF

Cubey's native SDF project has two current front doors: saved authored scenes
and an explicit material workshop. The public site presents the
[SDF forms](https://dev.byebyebryan.com/cubey/demos/sdf/), the
[shared material workshops](https://dev.byebyebryan.com/cubey/demos/materials/)
and the [construction story](https://dev.byebyebryan.com/cubey/demos/sdf/inside-a-solid/).
This guide covers native setup, recipes, behavior and limits. The detailed
[technical reference and study record](docs/technical-reference.md) retains older
recipes, diagnostics, evidence and historical anchors.

## Run

Build prerequisites and the development preset are in
[Build and run Cubey](../../docs/getting-started.md#configure-build-and-test).
Run the authored robot or the settled guardian directly:

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-limestone.json
```

Use `--headless`, `--time`, and `--output` for a saved still; `--config` selects
a project preset. Authored construction presets replay saved edits, and the
material workshop uses the same SDF renderer. The executable does not provide
continuous freehand sculpting.

## Material presentation

The public [material page](https://dev.byebyebryan.com/cubey/demos/materials/)
shows both renderers and the twelve shared recipes. The published
[SDF construction story](https://dev.byebyebryan.com/cubey/demos/sdf/inside-a-solid/)
connects a saved robot build with material cuts and explanatory diagrams. These
pages present native captures and saved replay; they do not demonstrate live
sculpting. The dated SDF material captures and source recipes remain in the
[immutable workshop media package](../../docs/media/material-workshops/README.md)
and [presentation evidence](../../docs/notes/material-presentation-evidence.md).
The retained native preset commands are indexed in the
[technical reference](docs/technical-reference.md#native-material-presentation-recipes).

### Frozen wood study

The saved wood-cut example remains runnable with
`projects/sdf/presets/presentation/wood-cut.json`. Its captured film, poster and
provenance are retained in the [material-workshop archive package](../../docs/media/material-workshops/README.md).
See the [detailed study record](docs/technical-reference.md#frozen-wood-study).

## Material workshop

`--workshop` opens an explicit specimen: `sphere`, `block`, `cut-block`,
`swatch`, or `csg`. It uses the standalone twelve-entry
[`assets/materials`](../../assets/materials) catalog, supports mesh/SDF
comparisons, and exposes recipe selection, cut ownership, smooth material
blending, pattern/bump switches, component diagnostics and save/reload. SDF v2
inline scenes remain readable; newly resolved material content uses authored
scene V3.

```sh
build/release/projects/sdf/sdf --workshop csg --material marble --yaw 0 --pitch .15 --distance 1.05
build/release/projects/sdf/sdf --workshop sphere --material glass --lighting material-atmosphere --sun-elevation 8 --sun-azimuth -155 --clouds --cloud-time 300 --cloud-coverage .28
build/release/projects/sdf/sdf --workshop cut-block --material wood
build/release/projects/sdf/sdf --workshop swatch --material woven-cloth
build/release/projects/sdf/sdf --workshop sphere --material frosted-glass
```

`--material-recipe` imports a resolved standalone recipe and
`--export-material-recipe` writes one. Selecting, resetting or reloading a
recipe replaces its edited values; changing specimens or CSG options preserves
current unsaved material edits. Material-only updates reuse the geometry cache.
The [shared recipe contract](../../docs/architecture/material-recipes.md) and
[library evidence](../../docs/notes/material-library-evidence.md) describe
serialization, coordinate transforms and the bounded optical paths.
The [workshop implementation record](docs/technical-reference.md#material-workshop-implementation-detail)
retains further diagnostic options and dated command recipes.

Glass measures in-medium distance along bounded interface tracing. Gallery
glass samples the environment after its own interfaces; it does not refract
neighboring displayed bodies. Frosted glass adds filtered environment sampling
on that path, not arbitrary scene refraction or a full rough BTDF. Shared
material recipes do not promise identical mesh and SDF light transport.

## Authored scenes

The saved robot and Guardian are native authored-scene examples. The Guardian
retains its accepted organic mirrored form, limestone and subtle Balanced
marble. Its 1,011 explicit primitive leaves make up 65 grouped history
operations; the leaf count includes sampled stroke segments, not independent
artistic actions. The
[Guardian closeout](../../docs/notes/sdf-guardian-closeout.md) records the
accepted appearance, review limits and source-pinned measurements.

The authored vocabulary includes boxes, spheres, capsules, cylinders, tori,
capped cones and rounded cones with union, subtraction and smooth variants.
Scripts can build, mirror and save atomic history operations. The windowed
history controls seek to a committed state with Previous/Next or the integer
Edit slider, pause on seek, resume with Play/Space, and return to the empty state
with Restart/R. Cut preview displays cutters as a color-only, depth-aware cue;
the published field supplies normal geometry and shadow queries. Export saves
the assembly, history and current material values.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit-build.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-mirrored-build.json
```

### Procedural stone surfaces

The current accepted limestone and restrained Balanced marble are summarized
in the [Guardian closeout](../../docs/notes/sdf-guardian-closeout.md). Earlier
material comparisons and technical detail remain in the
[study record](docs/technical-reference.md#procedural-stone-surfaces).

### Sphere and guardian material set

Current presets and appearance decisions are recorded in the
[Guardian closeout](../../docs/notes/sdf-guardian-closeout.md); older material
auditions are mapped in the
[technical reference](docs/technical-reference.md#sphere-and-guardian-material-set).

### Shared environment presentation

Authored scenes use Cubey's shared atmosphere and environment services. Dated
lighting and environment studies remain in the
[technical reference](docs/technical-reference.md#shared-environment-presentation).

### Authoring history

The [history reference](docs/technical-reference.md#authoring-history) retains the
authoring schema, exact reproduction commands, diagnostic contracts and dated
study sequence. The broader [authored-scene history](docs/technical-reference.md#historical-authored-scene-development)
is a record of earlier experiments, not an active backlog. Saved playback is a
presentation of committed edits, not an interactive brush editor.

## Limits and deferred work

Geometry edits rebuild the complete distance field synchronously. Material
edits reuse the geometry cache, but this does not make geometry editing
incremental. The accepted Guardian field allocation is about 871 MiB, and its
authored limits are 1,024 leaves, 2,048 instructions and sixteen materials;
these figures retain their original closeout scope. Replay can skip saved
actions while rebuilding and is not evidence of responsive continuous editing.

Desktop/VR brush sculpting, incremental field updates, global illumination,
terrain coupling, deeper rough-refraction work and AI-generated SDF content are
separate deferred choices. The settled Guardian geometry and limestone/subtle
marble appearance are closed to further realism work unless a new goal is
selected.

## Procedural recipes

Shared materials live in [`assets/materials`](../../assets/materials); authored
scene presets and saved recipe content live under `projects/sdf/presets/` and
`projects/sdf/scenes/`. The [technical reference](docs/technical-reference.md#procedural-recipes-and-retained-diagnostics)
retains the full procedural CSG, capture and qualification command catalog.

## Supporting evidence

- [Shared material and optical contract](../../docs/architecture/material-recipes.md)
- [SDF thread closeout and evidence map](../../docs/notes/sdf-thread-closeout.md)
- [Accepted Guardian closeout](../../docs/notes/sdf-guardian-closeout.md)
- [Material library evidence](../../docs/notes/material-library-evidence.md)
- [Material presentation evidence and archive links](../../docs/notes/material-presentation-evidence.md)
- [Detailed technical reference and study record](docs/technical-reference.md)
