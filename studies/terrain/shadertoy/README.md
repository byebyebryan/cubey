# Terrain ShaderToy Study

The ShaderToy terrain study is an optional, local-only fidelity tool. It compiles
selected external ShaderToy terrain sources without copying them into Cubey,
then transfers their height fields to one Cubey mesh and diagnostic harness for
controlled comparison. Mountains also retains its original raymarch control.

Configure with the default sibling reference checkout:

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies --target cubey_study_terrain_shadertoy
```

Override the source directory with `-DCUBEY_SHADERTOY_REF_DIR=/path/to/ShaderToy`.
When any required study source is absent, CMake skips the app and its tests.

The main local options are:

```text
--reference-study mountains|swiss-alps|mountain-peak|erosion-filter
--reference-render raymarch|mesh
--reference-time SECONDS
--reference-yaw-offset-deg DEGREES
--reference-mesh-cells 256|512|1024
--reference-mesh-surface terrain|map
--reference-normal geometry|atlas|detailed
--reference-shading original|clay
--reference-diagnostic final|height|slope|envelope|structure|uplift
```

Mountains retains the original defaults. The other studies default to mesh
rendering, terrain-only geometry, atlas normals, and clay shading. They reject
the Mountains-only raymarch, map/tree surface, detailed normal, and original
material options instead of silently producing a misleading hybrid.

`swiss-alps` transfers the medium- and high-octave derivative-damped terrain
field. `mountain-peak` retains the reference's radial attenuation and is an
audit-only example, not a candidate global source. `erosion-filter` applies the
external Phacelle erosion filter to the Mountains base and blends 25% of its
result back into the source field; the raw reference strength overwhelms this
base and is intentionally not presented as a viable default.

`terrain` is the five-octave base height. `map` includes the source's procedural
tree displacement. Atlas normals finite-difference the baked detailed-height
channel; detailed normals evaluate the additional six source octaves directly;
geometry normals expose mesh topology. Nonzero yaw offsets rotate the probed
source camera basis around world up while retaining its position, pitch,
elevation, and roll. They require final mesh rendering. Height and slope
diagnostics render the baked field from above and require the mesh path.
Envelope, structure, and uplift are Mountains-only spatial decompositions of
the exact baked height. They are broad low-pass, signed residual, and positive
broad-residual views respectively; they do not duplicate or reconstruct the
reference's internal formulas.

The windowed mesh path supports a lightweight inspection orbit around the
source camera target. Left-drag rotates, the mouse wheel zooms, and `R` restores
the exact configured reference view. Headless captures and the raymarch control
remain fixed.

Generate the fixed comparison pack with:

```sh
studies/terrain/shadertoy/capture_mountains_fidelity.sh
```

Generate the arbitrary-view, simplification, and GPU timing pack with:

```sh
studies/terrain/shadertoy/capture_mountains_generalization.sh
```

Generate the multi-source morphology pack with:

```sh
studies/terrain/shadertoy/capture_source_shape_studies.sh
```

The design, licensing boundary, and acceptance criteria are recorded in
[`docs/notes/terrain-shadertoy-mountains-fidelity.md`](../../../docs/notes/terrain-shadertoy-mountains-fidelity.md).
The multi-source scope and licensing boundary are recorded in
[`docs/notes/terrain-shadertoy-source-shape-studies.md`](../../../docs/notes/terrain-shadertoy-source-shape-studies.md).
