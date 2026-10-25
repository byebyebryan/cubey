# Material presentation captures

`library-dusk.json` and `library-dawn.json` frame all twelve shared recipes as a
four by three array of spheres. Matching SDF presets live in
`projects/sdf/presets/presentation/`. Both use a fixed 45-degree camera, sphere
radius 0.075, distance 0.855, sun azimuth -155 degrees, cloud coverage 0.28, and
128-pixel reflection probes. Mesh specimens use local recipe coordinates; the
SDF snapshots translate each recipe domain into that specimen's local space.

The eight-second lighting studies sweep sun elevation from 14 to -4 degrees
(dusk) or the reverse (dawn), with smoothstep timing and the shared atmosphere
exposure function. Clouds advance from 300 to 540 simulation seconds: an
accelerated artistic clock, not a geographic solar-day simulation. Specimen
shadows are disabled in both lineup presets. Renderer transport still differs.

```sh
python3 projects/pbr_materials/tools/author_library_showcase.py --check
build/release/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/presentation/library-dusk.json
build/release/projects/sdf/sdf --config projects/sdf/presets/presentation/library-dusk.json
build/release/projects/sdf/sdf --config projects/sdf/presets/presentation/cut-roster.json
```

The authoring script regenerates the native SDF documents and both sets of
presets from the material catalog. The cut board groups wood, marble, rusted
steel and clear glass into one saved two-step history. Cuts inherit their body
recipes; rust on the cut wall is the continuous field, not a fresh-steel cut
override. Clear glass uses the existing bounded, environment-only optical path.

SDF mixed-material presentation is explicit: `environment_only_gallery: true`
in a V3 scene permits separate sphere/box/cylinder bodies with inherited cuts.
Conservative body bounds must have a gap and fit inside the fixed SDF field.
Smooth blends, touching/overlapping/nested bodies, and cut-material overrides
are rejected. A glass path encountering a different gallery material after exit
resolves to the environment; it does not refract neighboring objects or solve
inter-object transport. Existing single-material scenes retain their old path.
