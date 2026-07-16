# Terrain ShaderToy Reference

`terrain_shadertoy_ref` is an optional, local-only fidelity study. It compiles
the external ShaderToy Mountains source without copying it into Cubey, renders
the original raymarch, and transfers the same source fields to a dense Cubey
mesh for controlled comparison.

Configure with the default sibling reference checkout:

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_terrain_shadertoy_ref
```

Override the source directory with `-DCUBEY_SHADERTOY_REF_DIR=/path/to/ShaderToy`.
When `mountains.glsl` is absent, CMake skips the app and its tests.

The main local options are:

```text
--reference-render raymarch|mesh
--reference-time SECONDS
--reference-yaw-offset-deg DEGREES
--reference-mesh-cells 256|512|1024
--reference-mesh-surface terrain|map
--reference-normal geometry|atlas|detailed
--reference-shading original|clay
--reference-diagnostic final|height|slope
```

`terrain` is the five-octave base height. `map` includes the source's procedural
tree displacement. Atlas normals finite-difference the baked detailed-height
channel; detailed normals evaluate the additional six source octaves directly;
geometry normals expose mesh topology. Nonzero yaw offsets rotate the probed
source camera basis around world up while retaining its position, pitch,
elevation, and roll. They require final mesh rendering. Height and slope
diagnostics render the baked field from above and require the mesh path.

The windowed mesh path supports a lightweight inspection orbit around the
source camera target. Left-drag rotates, the mouse wheel zooms, and `R` restores
the exact configured reference view. Headless captures and the raymarch control
remain fixed.

Generate the fixed comparison pack with:

```sh
projects/terrain_shadertoy_ref/capture_mountains_fidelity.sh
```

Generate the arbitrary-view, simplification, and GPU timing pack with:

```sh
projects/terrain_shadertoy_ref/capture_mountains_generalization.sh
```

The design, licensing boundary, and acceptance criteria are recorded in
[`docs/notes/terrain-shadertoy-mountains-fidelity.md`](../../docs/notes/terrain-shadertoy-mountains-fidelity.md).
