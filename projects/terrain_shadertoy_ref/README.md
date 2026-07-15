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
--reference-mesh-cells 256|512|1024
--reference-mesh-surface terrain|map
--reference-normal geometry|detailed
--reference-shading original|clay
--reference-diagnostic final|height|slope
```

`terrain` is the five-octave base height. `map` includes the source's procedural
tree displacement. Detailed normals evaluate the additional six source octaves;
geometry normals expose mesh topology directly. Height and slope diagnostics
render the baked field from above and require the mesh path.

Generate the fixed comparison pack with:

```sh
projects/terrain_shadertoy_ref/capture_mountains_fidelity.sh
```

The design, licensing boundary, and acceptance criteria are recorded in
[`docs/notes/terrain-shadertoy-mountains-fidelity.md`](../../docs/notes/terrain-shadertoy-mountains-fidelity.md).
