# Terrain ShaderToy Study

The ShaderToy terrain study is an optional, local-only fidelity source study. It
compiles selected external ShaderToy terrain sources without copying them into
Cubey, then retains their height/config/camera and mesh diagnostic logic for
controlled comparison. The former viewer and raymarch control are retired.

Configure with the default sibling reference checkout:

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies --target \
  cubey_study_terrain_shadertoy_reference_core cubey_study_terrain_shadertoy_tests
ctest --preset dev-terrain-studies -R '^terrain_shadertoy_ref_tests$' --output-on-failure
```

Override the source directory with `-DCUBEY_SHADERTOY_REF_DIR=/path/to/ShaderToy`.
When any required study source is absent, CMake skips the study and its tests.

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

The former windowed mesh path, headless capture scripts, and GPU profile test
were retired. Their shader/source evidence and the detailed configuration
vocabulary above remain for non-viewer comparison and future research.

The design, licensing boundary, and acceptance criteria are recorded in
[`docs/notes/terrain-shadertoy-mountains-fidelity.md`](../../../docs/notes/terrain-shadertoy-mountains-fidelity.md).
The multi-source scope and licensing boundary are recorded in
[`docs/notes/terrain-shadertoy-source-shape-studies.md`](../../../docs/notes/terrain-shadertoy-source-shape-studies.md).
