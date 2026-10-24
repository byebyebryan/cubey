# PBR material workshop

Ordinary mesh spheres, a beveled block, an oriented swatch and a bored block use `ForwardPbrRenderer3D` with the
twelve standalone recipes in [`assets/materials`](../../assets/materials).
The [reviewed plan](../../docs/notes/pbr-material-demo-plan.md) and
[SDF workshop contract](../../docs/notes/sdf-material-workshop-plan.md) define scope.

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

Glass uses the existing forward volume/refraction route and a 0.5-unit specimen
thickness estimate. The SDF adapter measures in-medium distances instead. V1
shared glass remains plain neutral, roughness 0.05, with editable IOR and
attenuation. It does not introduce a new transport solver.

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
New visual presentation remains open to user review.

Reproduce the bounded native matrix with
`python3 projects/pbr_materials/tools/qualify_workshops.py --stage all`.
The report builder is `projects/pbr_materials/tools/build_workshops_report.py`;
its compatibility/diagnostic inputs are documented in the evidence record.

The expansion uses `projects/pbr_materials/tools/qualify_library.py` and
`build_library_report.py` in the same directory. The new qualification discovers
the shared catalog and retains native command, binary, shader and recipe receipts.
