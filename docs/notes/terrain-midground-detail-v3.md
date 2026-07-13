# Terrain Midground Detail V3

Date: 2026-07-12

Status: implemented and measured opt-in renderer checkpoint. The existing
`quality + tile` path paired with source v2 remains the reproducible control.

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

## Implemented Checkpoint

The candidate landed as `terrain.surface_detail=layered`, valid only with the
mountain quality renderer. It adds:

- separate deterministic `backdrop` (3.2/6.4 km) and `midground` (1.6 km)
  camera profiles;
- bounded anisotropic sampler support in the shared Vulkan renderer;
- four generated albedo-height and normal-roughness-cavity material pairs;
- explicit-gradient warped triplanar sampling, height-assisted transitions,
  source-normal recovery, and footprint-driven normal fade;
- material roughness, height, and cavity diagnostics;
- a fixed A/B, motion, identity, timing, and memory review script.

The generated recipes use periodic gradient noise rather than exposing a
square value-noise lattice. Broad ground relief and color are intentionally
subdued. Rock combines warped and rotated gradient octaves rather than an
absolute-value ridge field, because the latter drew closed contour loops over
broad faces. These are material-domain corrections only; source and geometry
remain frozen.

Generate the accepted pack with:

```sh
projects/terrain/capture_midground_detail_review.sh
```

It replaces `outputs/terrain/midground-detail-v3/` and records:

| Measure | Tile | Layered | Result |
| --- | ---: | ---: | ---: |
| changed height pixels | - | - | `0` |
| material-normal Laplacian energy | `3.8645e9` | `4.99579e9` | `1.2927x` |
| observed 960 x 540 frame interval | `22.3009 ms` | `23.8544 ms` | `+1.5535 ms` |
| device-local use | `52.00 MiB` | `73.25 MiB` | `+21.25 MiB` |

The source-report hashes remain unchanged for v1 and v2. Validation-enabled
native captures cover backdrop seeds `0`, `9012`, and `12345`, plus midground
seeds `9012` and `12345`. A 90-frame moving midground capture checks temporal
behavior. Seed `0` retains its macro read; seeds `9012` and `12345` gain visible
face structure without changing silhouette or height.

## Remaining Boundary

The candidate is accepted as an opt-in renderer study, not promoted to the
default. It remains mountain-only and intentionally stops at the 1.6 km
midground tier. Generated periodic materials, material classification, and the
simple source still fall short of close terrain, authored geology, debris,
water, or vegetation geometry. The next decision should come from a real scene
consumer rather than another isolated resolution increase.

## Validation

The final validation-enabled review pack passed its source-hash, height-identity,
detail-band, frame-budget, and device-memory gates. Focused terrain render and
layered PNG tests passed `3/3` in `18.29 s`. The full dev build completed and the
repository suite passed `226/226` tests in `1136.48 s`, including atmosphere,
cloud, ocean, planet, fluid, active terrain, hydrology-lab, reference, and
legacy gates.
