# Shared material recipes

A recipe is material content, independent of mesh/SDF geometry, lighting and
cameras. `assets/materials` holds seven initial accepted looks. The PBR workshop
and explicit SDF workshop mode read these files directly; legacy inline SDF v2
materials remain supported. Importing a recipe resolves its values rather than
creating a live link to a mutable global asset.

`cubey.material.recipe` version 1 has an `id`, `label`, `surface` and optional
`material_from_object` (identity by default). Colors are linear RGB. `surface`
contains base color, perceptual roughness, emission, optical values and optional
limestone/marble/granular parameters. Unknown fields, nonfinite values, invalid
ranges, singular/projective domains and unsupported mixed glass are rejected.
Serialization is deterministic; the resolved serialized value supplies a stable
64-bit content fingerprint. No camera, mesh, CSG tree, weather or exposure belongs
in a recipe. See [marble](../../assets/materials/marble.json) for a complete example.

`cubey::asset` owns strict parsing/persistence. `cubey::procedural` owns the
32-byte surface, 80-byte pattern, 32-byte optics and pure CPU/GLSL evaluators.
`cubey::pbr_material_from_recipe` is an engine adapter so render does not depend
on asset parsing. SDF retains compatibility aliases to those same value types.

Patterns sample an explicit material-space point and pixel footprint. Their
result is color, roughness, emission and an analytic height gradient. The affine
column-major domain maps object positions into material units. Gradients use
its transposed Jacobian and then the object-to-world inverse transpose. Meshes
compute transformed screen derivatives; SDF scalar footprints use a conservative
Frobenius bound for nonidentity domains. Frequency filtering fades unresolved
noise and bounds marble coverage. Neither adapter normalizes by model bounds.
Optical attenuation distance remains independently in geometry units.

The forward PBR material uniform block retains its original prefix and appends
procedural options, pattern and domain (896 bytes total). Texture bindings and
the default image-based/zero-rough-diffuse behavior remain unchanged. All shader
consumers must rebuild with the new uniform size. SDF identity domains preserve
legacy fingerprints; only nonidentity domains add a tagged appearance-hash
component. Geometry hashes exclude appearance and domain values.

PBR material tables remain immutable by default. The explicit
`editable_after_retire` option permits updating an existing uniform range and
CPU definition after **all readers of that table have retired**. The caller must
own one table per in-flight slot or provide equivalent retirement. The workshop
uses separate preallocated tables, seven permanent handles and host frame fences.
Updates cannot change registry routing (alpha, transmission, cull, sort or pass
mask); selecting glass uses its prepublished handle. No slider allocates a new
descriptor or uniform buffer. SDF retains its existing queue-idle appearance
publication, with distance-field cache reuse.

Shared V1 glass is plain neutral white with roughness 0.05, zero metallic,
emission and diffuse roughness. IOR and Beer-Lambert attenuation are content.
The mesh adapter supplies an explicit specimen thickness estimate to the existing
forward volume route; SDF measures interface segments with a selected crossing
budget. The same optical recipe does not imply identical image transport.
UV image authoring, arbitrary material graphs, nested media and caustics remain
outside this material-content milestone.
