# Procedural Generation Foundation

Cubey uses procedural assets across terrain, planet detail, ocean-adjacent
fields, fluids, clouds, atmosphere backdrops, generated textures, and validation
scenes. Terrain Lab has made the shared problem clearer: procedural work should
start from coherent source fields and reusable operators, not from slice-local
hand-authored masks that are later dressed with materials or shader noise.

## Direction

The near-term goal is a CPU/reference procedural foundation that project code can
use before a GPU or node-graph version exists. It should provide deterministic
building blocks:

- scalar field containers or views for local 2D grids;
- seeded noise sources and domain warping;
- common field operators such as remap, clamp, blur/smooth, ridge, terrace, and
  composition helpers;
- summary/stat helpers for tests and debug views;
- hydrology-oriented utilities once the river and SimpleHydrology reference
  review is complete.

The first shared layer should stay data-oriented and renderer-independent. It
should not own Terrain Lab policy, planet LOD, ocean shoreline contracts,
materials, foliage, or Vulkan resources.

## Terrain Lessons

Terrain Lab should remain the visual pressure test for these pieces, but the
pieces should not remain trapped inside Terrain Lab if two projects can use
them. The current rule is:

```text
source field -> reusable operators -> feature/process driver -> project recipe
```

Hand-authored local lines, disks, quadrants, and one-off masks are acceptable
only as temporary scaffolding while a source model is being discovered. They are
not acceptable as the final driver for terrain, cloud structure, fluids, or any
other procedural asset that needs to scale beyond one demo camera.

## Reference Strategy

Before adding major foundation pieces, inspect proven references or libraries
and decide explicitly whether to reuse, adapt, or avoid them. For terrain and
hydrology, start with:

- `~/code/ref/TerrainEngine-OpenGL` for presentation, tiling, and material
  context;
- `~/code/ref/SimpleHydrology` for process-state and hydrology diagnostics;
- published hydrology and erosion work already cited by Terrain Lab.

The first integration pass should favor small, deterministic, testable utilities
over a full node graph, full hydraulic simulator, or heavy erosion pipeline.

## Current Foundation Slice

The first shared CPU-side layer lives under `include/cubey/procedural` and is
compiled into `cubey::core`:

- `Grid2DDesc` and `ScalarField2D` provide local 2D scalar-field storage,
  centered sample coordinates, bounds-checked indexing, and field summaries.
- `operators.h` provides scalar helpers (`saturate`, `lerp`, `smoothstep`) plus
  first-pass field operators (`box_blur_3x3`, `normalize_to_unit`).
- `noise.h` provides deterministic 2D hash/value-noise, FBM, and ridged FBM
  with explicit octave/lacunarity/gain/seed-stride configuration.

Terrain Lab now consumes this shared layer for its scalar helpers and
deterministic FBM source. That adoption is intentionally conservative: it
removes duplicated hash/noise/operator code without changing the visual driver
model in the same batch.

Next shared candidates should come from repeated project-local code or from a
specific reference-backed driver need, not from speculative API surface. Likely
near-term candidates are:

- reusable slope/curvature and local-relief operators;
- terrain-oriented ridge/terrace/remap composition helpers;
- flow-routing and accumulation data structures after a deeper SimpleHydrology
  pass;
- explicit source-field recipes for mountain range, river, and dune drivers.

## Adoption Rule

Promote a helper from a project into `cubey::procedural` only when it is:

- deterministic and testable without a renderer;
- useful to at least two projects or clearly foundational for one project and an
  integration target;
- named by terrain/process meaning rather than by one slice's visual outcome;
- documented with limits so downstream projects do not mistake an approximate
  driver for a physical simulation.
