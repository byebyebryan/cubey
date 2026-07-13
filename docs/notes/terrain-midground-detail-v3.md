# Terrain Midground Detail V3

Date: 2026-07-12

Status: planned A/B renderer batch. The existing `quality` path paired with
source v2 remains the reproducible control.

## Read Of The Current Pack

The resolution prototype established enough macro quality for the terrain v1
backdrop contract. Seed `0` reads credibly when the mountain mass stays in the
far field. Seed `9012` targets the same approximate `3.2 km` distance but puts
one broad face across much more of the frame, exposing weak surface response.
Seed `12345` targets approximately `1.6 km` and enters an intentionally harder
midground tier.

These are different tests:

- far backdrop, at `3.2 km` or farther, judges silhouette, atmosphere, and
  broad material separation;
- midground, fixed at `1.6 km`, judges rock, scree, snow, and normal bandwidth;
- near ground remains unsupported terrain v1 presentation and must not drive
  this batch toward foliage, stones, or walkable-surface fidelity.

The selected distance is not sufficient by itself. Seed `9012` remains a
required stress case because a distant face can still occupy enough pixels to
expose smooth or cloudy material structure.

## Reference Direction

The local TerrainEngine, Terrain3D, 3DWorld, and selected ShaderToy examples
converge on a common split rather than a more elaborate height generator:

- preserve a low-frequency height source for geometry and collision;
- recover terrain form per fragment or from a height/normal cache;
- add distinct tiled material layers with albedo, height, normal, roughness,
  and bounded ambient response;
- filter and fade those layers by screen footprint;
- use a separate distant macro presentation and closer detail presentation.

TerrainEngine obtains much of its screenshot quality from per-fragment source
normals, imported material textures, and strong directional lighting. Cubey
will borrow the architecture, not its assets. Terrain3D's packed
albedo-height/normal-roughness contract and height-assisted blending are the
closest practical reference for a code-centric mesh renderer. ShaderToy's
low-detail geometry plus higher-detail normal evaluation supports the same
division, but its raymarching and restricted source code are not copied.

## Frozen Boundary

This batch does not change:

- source v1 or v2 parameters, hashes, composition, or weathering;
- clipmap topology, tessellation factors, geometry positions, or collision;
- macro silhouettes or terrain preset definitions;
- hydrology, erosion, water, vegetation geometry, or scene integration;
- the default `control` renderer or current `quality` material path.

The new renderer is an opt-in surface-detail candidate over the existing
mountain quality path. It must remain possible to render the current path from
the same executable and camera poses.

## Candidate Contract

The candidate adds four procedural material layers: ground, scree, rock, and
snow. Each layer owns two periodic `1024 x 1024` textures spanning `256 m` with
eleven mips:

- linear albedo RGB plus normalized blend height;
- encoded tangent normal XY, roughness, and bounded cavity.

The material recipes must be visibly distinct. Ground uses subdued soil
variation, scree uses multi-scale cellular aggregate, rock uses warped strata
and fractures, and snow uses smooth deposited breakup. Height may refine an
existing material transition but cannot override the slope/elevation-derived
macro classification.

The candidate requests `8x` anisotropic sampling and uses explicit texture
gradients for warped triplanar projection. Unsupported devices fall back to
trilinear filtering rather than rejecting the terrain renderer.

Source-normal recovery uses central source samples with an initial sample step
of `clamp(2 * pixel_footprint, 2 m, 12 m)`. Its contribution is capped at
`60%`, remains full through `3 m/pixel`, and fades to zero by `8 m/pixel`.
Generated material mips carry the remaining local shading detail. Neither path
displaces geometry.

## Camera And Review Contract

The production `backdrop` profile selects mountain targets only at `3.2 km` or
`6.4 km`. A separate deterministic `midground` profile selects `1.6 km` so
detail remains tested instead of being hidden by the backdrop floor. Both
profiles retain the existing `150 m` camera-clearance and `300 m`
lower-frustum-clearance guarantees.

The fixed A/B pack compares current `quality + tile` against
`quality + layered`, both on source v2. It includes:

- backdrop seeds `0`, `9012`, and `12345`;
- midground seeds `9012` and `12345`;
- matched native-resolution crops and material/normal diagnostics;
- a moving midground capture for aliasing and shimmer;
- timing, memory, source-hash, and height-identity evidence.

Seed `0` must keep its clean macro read. Seed `9012` must gain coherent broad
face structure rather than cloudy color or noisy silhouettes. Seed `12345`
must expose more convincing detail without visible tiling, speckle, or
high-frequency geometric noise. The candidate remains opt-in after this batch;
default promotion and external scene integration are later decisions.
