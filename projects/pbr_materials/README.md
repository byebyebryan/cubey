# PBR material workshop

Ordinary mesh spheres and a beveled block use `ForwardPbrRenderer3D` with the
seven standalone recipes in [`assets/materials`](../../assets/materials).
The [reviewed plan](../../docs/notes/pbr-material-demo-plan.md) and
[SDF workshop contract](../../docs/notes/sdf-material-workshop-plan.md) define scope.

```sh
cmake --build build/release --target cubey_project_pbr_materials
build/release/projects/pbr_materials/pbr_materials
build/release/projects/pbr_materials/pbr_materials --material marble --lighting clouds
build/release/projects/pbr_materials/pbr_materials --headless --material clay --specimen block --output outputs/clay.png
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
Each holds seven permanent material handles; selecting opaque/glass switches
handles. Routing changes within a handle are rejected before publication.
`--stress-edits 1000` exercises each used slot without growing UBO or descriptor
residency. Existing imported materials remain immutable by default.

[Qualification and native review report](../../docs/notes/material-workshops-evidence.md):
98 images, five eight-second films, retained control images/fingerprints,
shared recipe round trips, native editing checks and measured 1080p costs.
The seven-material mesh gallery measures 0.303 ms in studio and 2.607 ms with
clouds on the qualified RTX 5070 Ti run; setup and encoding are separate.
New visual presentation remains open to user review.

Reproduce the bounded native matrix with
`python3 projects/pbr_materials/tools/qualify_workshops.py --stage all`.
The report builder is `projects/pbr_materials/tools/build_workshops_report.py`;
its compatibility/diagnostic inputs are documented in the evidence record.
