# Shared material recipes

A recipe is material content, independent of mesh/SDF geometry, lighting and
cameras. `assets/materials` holds twelve looks: the accepted seven V1 recipes
and five opt-in V2 surface studies. The PBR workshop
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

The forward PBR material uniform block retains its original prefix, followed by
procedural options, pattern and domain through byte 896, then the separate
144-byte library record (1040 bytes total). Texture bindings and
the default image-based/zero-rough-diffuse behavior remain unchanged. All shader
consumers must rebuild with the new uniform size. SDF identity domains preserve
legacy fingerprints; only nonidentity domains add a tagged appearance-hash
component. Geometry hashes exclude appearance and domain values.

PBR material tables remain immutable by default. The explicit
`editable_after_retire` option permits updating an existing uniform range and
CPU definition after **all readers of that table have retired**. The caller must
own one table per in-flight slot or provide equivalent retirement. The workshop
uses separate preallocated tables, one permanent handle per validated catalog
entry and host frame fences.
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

## Library expansion data

The [library expansion](../notes/material-library-expansion-plan.md) adds an
explicit recipe version 2. Its `library` object has named wood, rust, weave and
paper parameter groups, seed/scale/relief, secondary color, steel/oxide roughness,
sheen color/roughness and an opt-in `rough_glass` flag. Version 1 retains its
serialized values, restrictions and fingerprints. A recipe cannot combine a
legacy procedural pattern with a new library pattern. New parameters compile
to a separate checked 144-byte `SurfaceLibrary`, preserving legacy strides.

`assets/materials/catalog.json` is the shared bounded catalog (maximum 32 entries).
Loading validates all entries, relative filenames and recipe identities before
returning a candidate snapshot; callers replace their current snapshot only on
success. Metadata is independent of recipe appearance fingerprints.

SDF authored scene version 3 uses material records containing only `name` and a
self-contained `recipe`. The version 2 inline path remains unchanged. New
library values contribute a tagged appearance-hash domain and never alter the
geometry fingerprint. Export refuses to discard new material data through the
old inline format.

The shared library evaluators now supply filtered wood, rust, weave and paper
surfaces in CPU and GLSL. Resolved samples add spatial metalness (a negative
sentinel inherits constant optics), sheen, and the explicit rough-glass flag.
Rust correlates metalness, color, roughness and relief. Sheen uses the existing
shared Charlie helpers and generated IBL LUT alpha in both consumers. SDF
material ownership/blending carries those resolved response channels.

Wood/rust use solid coordinates; cloth/paper use explicit material X/Y axes on
oriented swatches. A general surface atlas or seamless cloth drape is not supplied.
Relief gradients obey the same affine chain rule as V1. Periodic/fiber detail
fades under minification while the rough response remains available.

V2 frosted glass is opt-in, with plain white base, zero metal/emission/sheen and
rough diffuse, plus editable roughness/IOR/absorption. Mesh reuses forward rough
transmission. SDF estimates angular spread along the bounded central interface
path and samples filtered environment radiance; it does not add arbitrary scene
refraction or a full rough BTDF solver. Existing V1 glass follows its sharp path.
See the [native expansion evidence](../notes/material-library-evidence.md).
