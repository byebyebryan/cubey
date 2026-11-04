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
Run the authored robot directly:

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json
```

Use `--headless`, `--time`, and `--output` for a saved still; `--config` selects
a project preset. Authored construction presets replay saved edits, and the
material workshop uses the same SDF renderer. The executable does not provide
continuous freehand sculpting.

## Rendering modes

Saved authored scenes, the material workshop and box recipes default to
**Cached**, using the existing cached primary rays, normals and shadows.
Balanced remains available for refined cached hits and source CSG normals.
The native **Rendering** selector and `--render-mode` expose every
supported alternative:

| Mode | Use |
| --- | --- |
| `auto` | Cached for authored scenes, workshops and box recipes |
| `cached` | Original cached rendering, maximum speed |
| `cached-refined` | Improve cached hit positions with little extra work |
| `balanced` | Refined cached hits plus source normals |
| `analytic` | Source-evaluated geometry, normals and secondary rays |
| `hybrid` | Existing cached-bound traversal with source evaluation near surfaces |

Balanced improves represented surfaces; it cannot recover thin geometry absent
from the cache. Analytical and hybrid remain available for source detail.
Analytical atmosphere rendering uses A64 root-far acceleration. Material/optical
analytical rendering and hybrid retain ordinary A16 pruning. Prepared-cone and
adaptive stepping studies retain their separate admission rules.

**Supersampling is independent.** The native selector and `--supersample 1|2`
change sampling without changing mode or rebuilding geometry. Existing presets
retain their sample settings, including SS2 on the robot. SS2 has a
substantial cost; use SS1 for a lower frame budget. Mode changes preserve
camera, playback position and material edits. Cached-to-cached mode changes
reuse the field. Analytical-only releases it; returning to cached rendering
can incur a full rebuild.

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json --render-mode balanced --supersample 1
build/release/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json --render-mode analytic
build/release/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json --render-mode cached
```

Existing explicit low-level tracing flags retain their original behavior when
no named mode was requested. Contradictory named-mode/low-level settings reject
with an option-specific error. Unsupported modes are disabled with a reason
for recipe scenes or incompatible diagnostic views. Numerical ablations remain
available through their existing advanced study flags.

The [promotion plan](../../docs/notes/sdf-render-mode-promotion-plan.md) and
[execution record](../../docs/notes/sdf-render-mode-promotion-progress.md) retain
the qualification scope. GPU measurements are separate from physical display
latency and human visual acceptance.

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

The field-kit robot is the featured authored CSG example. Its finished assembly
and saved construction history show how primitives, additive/subtractive
operations and materials compose a solid. The
[CSG milestone closeout](../../docs/notes/sdf-csg-closeout.md) records the
supported scope, retained research and deferred editing questions.

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
```

### Retained sculpture studies

[Guardian](../../studies/sdf/guardian/README.md) is retired from the active demo
collection after renewed visual review. Its scenes, presets and dedicated tools
live in studies; it remains an explicit stress fixture for the recorded tracing,
materials and edit-service work. Earlier sculpture/content auditions remain
experiments rather than additional selected demos. Dated results and captures
retain their historical scope and identities.

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

Public Cached-mode geometry edits rebuild the complete distance field synchronously.
Material edits reuse the geometry cache. The default field allocation is about
871 MiB, with authored limits of 1,024 leaves, 2,048 instructions and sixteen
materials. Replay can skip saved
actions while rebuilding and is not evidence of responsive continuous editing.

The separate [bounded edit-service research](../../docs/notes/sdf-edit-service-results.md)
qualifies private local updates and finite timestamped input on a simple scene.
It does not supply a public brush editor or sustained complex-scene editing.
Source growth, grouped live-stroke undo, general editing controls and broader
reader lifetime remain deferred research. VR is not a current target.

Global illumination, terrain coupling, deeper rough refraction and native
mesh-to-field integration are separate choices. The authored CSG milestone is
closed around the robot and workshop; shared rendering improvements can still
benefit those examples.

## Procedural recipes

Shared materials live in [`assets/materials`](../../assets/materials); authored
scene presets and saved recipe content live under `projects/sdf/presets/` and
`projects/sdf/scenes/`. The [technical reference](docs/technical-reference.md#procedural-recipes-and-retained-diagnostics)
retains the full procedural CSG, capture and qualification command catalog.

## Supporting evidence

An opt-in [tracing/cache comparison](../../docs/notes/sdf-tracing-comparison-results.md)
adds authored `--surface-trace analytic|hybrid` and `--csg-evaluation pruned`
with `--region-axis 4|16|64`. The original study retained `sampled` / `full`; these study
routes have measured quality/memory benefits and content-dependent performance
limits. [Commands and receipts](../../docs/evidence/sdf-tracing-comparison-2026-10/README.md)
include the bounded query, undo and native inspection tools.

The opt-in source paths now use [automatic shader selection](../../docs/notes/sdf-shader-selection-results.md):
prebuilt lit/debug and eligible one-material pipelines follow published material
state and current view settings. `--source-shaders general` selects the original
source shader for comparison. The retired Guardian benchmark improved
substantially but remained above its dated 1080p GPU target; startup compilation
can be expensive. Current defaults
and explicit rendering choices are listed above.

Source-query controls are documented in the
[optimization loop](../../docs/notes/sdf-source-query-optimization-progress.md).
`--source-optimization automatic` now skips zero-contribution shadows on the
eligible opaque one-material lit source path. `reference` retains the previous
automatic shader, while `--source-shaders general` keeps the original complete
shader. `prepared-cones` and `combined` are explicit static headless
analytic/pruned A64 root-far studies; they preserve the source/history format
and existing geometry tolerances. These advanced studies retain their private
admission. The public Analytical atmosphere mode separately qualifies ordinary
root-far rendering; explicit legacy tracing semantics remain unchanged.

The [guarded-step investigation](../../docs/notes/sdf-query-acceleration-progress.md)
retains further explicit headless query studies. Their numerical qualification,
frame-time measurements and visual-adoption limits are separate from the
automatic behavior above.

- [Shared material and optical contract](../../docs/architecture/material-recipes.md)
- [SDF thread closeout and evidence map](../../docs/notes/sdf-thread-closeout.md)
- [Authored CSG milestone closeout](../../docs/notes/sdf-csg-closeout.md)
- [Retired Guardian study](../../studies/sdf/guardian/README.md)
- [Material library evidence](../../docs/notes/material-library-evidence.md)
- [Material presentation evidence and archive links](../../docs/notes/material-presentation-evidence.md)
- [Detailed technical reference and study record](docs/technical-reference.md)
