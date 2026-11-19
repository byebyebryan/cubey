# PBR material workshop

Ordinary mesh spheres, a beveled block, an oriented swatch and a bored block use `ForwardPbrRenderer3D` with the
twelve standalone recipes in [`assets/materials`](../../assets/materials).
The [reviewed plan](../../docs/notes/pbr-material-demo-plan.md) and
[SDF workshop contract](../../docs/notes/sdf-material-workshop-plan.md) define scope.
The current visual collection is on the
[Materials page](https://dev.byebyebryan.com/cubey/demos/materials/#mesh-materials);
this guide remains the native run and editing reference.

## Temporal anti-aliasing

Interactive AA Auto uses native TAA for opaque rigid specimens with frozen
lighting. Use the explicit `taa` setting when reproducing a comparison.

```sh
build/release/projects/pbr_materials/pbr_materials --material woven-cloth --specimen swatch --anti-aliasing taa
build/release/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/taa-foundation-2026-11/rigid-motion-taa.json
```

The moving fixture holds, crosses a patterned board and thin bronze bars, stops,
then returns. Its camera starts fixed; R restarts the fixture and history.
`--taa-fixture-time 3.5` freezes a reproducible pose while submitted frames still
advance TAA. Use the matching `rigid-motion-off.json` for comparison. The
preset directory also contains matched clay, cloth, wood and bronze controls.

The Anti-aliasing selector is at the end of the workshop panel. Unsupported
glass, clouds, dynamic lighting and raw diagnostic/readback paths show a bypass
reason and retain the original unjittered image. Fine detail can soften during
motion; TAA remains separate from the existing material/optical recipes and
display exposure. The [shared foundation contract](../../docs/architecture/temporal-aa.md)
describes history, motion, admission and validation.

## Presentation

The [public material showcase](https://dev.byebyebryan.com/cubey/demos/materials/)
puts all twelve spheres in one view, with moving clouds and matching forward
PBR / SDF dusk and dawn films. Run either forward preset:

```sh
build/release/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/presentation/library-dusk.json
build/release/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/presentation/library-dawn.json
```

See the [matched native presentation guide](presets/presentation/README.md) for
camera, lighting, recipe coordinates and optical limits.

### Frozen four-specimen study

This retained native preset is a dated capture setup, not the current public
gallery. Its poster, film, native master, recipe and provenance remain in the
[material-workshop archive package](../../docs/media/material-workshops/README.md)
and [presentation evidence](../../docs/notes/material-presentation-evidence.md).
Run the preset locally:

```sh
build/release/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/presentation/contrast.json
```

Controls start collapsed; expand the title bar for pause/replay and the full
material workshop. Space pauses, R restarts, and drag/scroll still orbit/zoom.
`--presentation-time 4` selects a reproducible snapshot. The alternative
`surfaces.json` preset retains the frosted-glass group audition. The archive
package above retains both 1080p masters and 720p clips with capture provenance.

## Workshop

```sh
cmake --build build/release --target cubey_project_pbr_materials
build/release/projects/pbr_materials/pbr_materials
build/release/projects/pbr_materials/pbr_materials --material marble --lighting clouds
build/release/projects/pbr_materials/pbr_materials --headless --material clay --specimen block --output outputs/clay.png
build/release/projects/pbr_materials/pbr_materials --material wood --specimen cut-block
build/release/projects/pbr_materials/pbr_materials --material woven-cloth --specimen swatch
build/release/projects/pbr_materials/pbr_materials --material frosted-glass --lighting clouds
```

Select a recipe to inspect and edit it; drag/scroll to orbit/zoom. Pattern and
bump toggles, component views, reset and recipe save/reload provide bounded
experiments. Colors are linear. `--recipe-input` and `--recipe-output` operate on
resolved standalone recipes. `--roughness`, `--pattern-scale`, `--yaw`, `--pitch`
and `--orbit-degrees` support repeatable captures. Lighting is `studio` (default,
no atmosphere/cloud allocation), `clear`, or frozen `clouds` using the accepted
shared low-sun settings. No terrain or ground plane is introduced.

Glass uses the existing forward volume/refraction route with a thickness estimate
for the selected specimen: 0.5 units for sphere/block, 0.036 for the swatch and
0.32 for the bored block. The SDF adapter measures in-medium distances instead. V1
shared glass remains plain neutral, roughness 0.05, with editable IOR and
attenuation. It does not introduce a new transport solver.

`--transmission-source environment` resolves the distant probe along the
estimated exit ray; `screen` retains the default camera-image lookup with edge
fallback. The [RT comparison presets](presets/rt-comparison/gallery-clear.json)
explicitly select environment transmission under the shared clear atmosphere.
The UI source selector and **Save view** (`--view-output` for headless captures)
retain this choice without changing recipes. See the
[shared workshop guide](../../docs/notes/raytracing-material-workshop-guide.md)
for replay, raw HDR diagnostics and optical limits.

Live edits use one preallocated opt-in material table per retired frame slot.
Each holds one permanent handle per catalog entry; selecting opaque/glass switches
handles. Routing changes within a handle are rejected before publication.
`--stress-edits 1000` exercises each used slot without growing UBO or descriptor
residency. Existing imported materials remain immutable by default. Family
filtering and the four-column gallery scale selection beyond the original seven.
Library reload validates the complete candidate; roster/order/optical-route
changes require restart to preserve permanent handles.

The [library expansion evidence](../../docs/notes/material-library-evidence.md)
covers wood, rusted steel, kraft cardboard, woven cloth and frosted glass in both
consumers. Twelve-material mesh gallery costs are 0.452 ms in studio and 2.089 ms
with clouds on the qualified 1080p RTX 5070 Ti run. These are bounded GPU scene
measurements; setup and encoding are separate. Recipe V2 and resolved SDF scene
V3 preserve new parameters through save/reload. V1 content remains unchanged.

[Original seven-material qualification and review report](../../docs/notes/material-workshops-evidence.md):
98 images, five eight-second films, retained control images/fingerprints,
shared recipe round trips, native editing checks and measured 1080p costs.
The seven-material mesh gallery measures 0.303 ms in studio and 2.607 ms with
clouds on the qualified RTX 5070 Ti run; setup and encoding are separate.
These figures and review statements retain the evidence record's original
source, device and scope; the public visual presentation is linked above.

Reproduce the bounded native matrix with
`python3 projects/pbr_materials/tools/qualify_workshops.py --stage all`.
The report builder is `projects/pbr_materials/tools/build_workshops_report.py`;
its compatibility/diagnostic inputs are documented in the evidence record.

The expansion uses `projects/pbr_materials/tools/qualify_library.py` and
`build_library_report.py` in the same directory. The new qualification discovers
the shared catalog and retains native command, binary, shader and recipe receipts.
