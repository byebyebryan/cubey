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

## Consumer Taxonomy

Procedural consumers should be grouped by the role the randomness plays before
code is promoted into shared helpers:

- Coherent source fields: terrain height, macro terrain masks, cloud shape
  fields, and similar signals where neighboring samples must relate spatially.
- Stochastic hash/random helpers: deterministic per-particle jitter, star-cell
  selection, tie-breakers, and sampling variation where continuity is not the
  point.
- Domain recipes: higher-level combinations such as dune shaping, river
  drivers, moon terminator breakup, ocean foam, pyro turbulence, cloud volume
  recipes, and Milky Way atlas construction.
- Reference and legacy snapshots: code kept close to a paper, reference project,
  or old renderer so it can still be compared directly.

The migration rule differs by category. Shared hash and value-noise primitives
can be migrated mechanically when formulas match exactly. Domain recipes should
stay project- or domain-owned unless the shared abstraction names the process
being modeled, not just the visual result of one demo. Reference and legacy
snapshots should not be deduplicated until the comparison value is gone.

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

For coherent noise specifically, Cubey should use FastNoiseLite as the first
shared dependency. It is small, MIT-licensed, header-only for C++, has matching
GLSL/HLSL ports for later shader parity, and covers the immediate source-field
needs: OpenSimplex, Perlin, value, cellular/Voronoi, fractal variants, and
domain warping. FastNoise2 remains worth a later spike if CPU-side field
generation needs SIMD throughput or a node-graph representation, but it is too
large for the first foundation step.

The initial FastNoiseLite integration should be wrapped by `cubey::procedural`
instead of exposed directly to projects. Existing hash/value-noise functions
remain available as a legacy deterministic backend so old captures and tests do
not silently change.

## Current Foundation Slice

The first shared CPU-side layer lives under `include/cubey/procedural` and is
compiled into `cubey::core`:

- `Grid2DDesc` and `ScalarField2D` provide local 2D scalar-field storage,
  centered sample coordinates, bounds-checked indexing, and field summaries.
- `operators.h` provides scalar helpers (`saturate`, `lerp`, `smoothstep`,
  `smootherstep01`) plus first-pass field operators (`box_blur_3x3`,
  `normalize_to_unit`).
- `noise.h` provides Cubey-wrapped FastNoiseLite coherent noise/domain-warp
  sampling plus deterministic legacy 2D and 3D hash/value-noise, FBM, and
  ridged FBM with explicit octave/lacunarity/gain/seed-stride configuration.
- `shaders/cubey/procedural` mirrors the small GLSL side of this layer with
  shared remap/smoothing helpers, deterministic random/hash helpers, and
  value-noise/FBM helpers for formulas that already match existing project code.

Terrain Lab now consumes this shared layer for its scalar helpers and
deterministic FBM source. It also exposes an opt-in FastNoiseLite backend for
the desert dune source driver. That adoption remains intentionally
conservative: default captures keep the legacy backend, while the new coherent
noise path can be inspected explicitly.

The first preserve-output migration wave also routes existing duplicate helpers
through shared primitives where the formulas already matched:

- `projects/procedural_terrain` uses shared CPU scalar and deterministic FBM
  helpers while preserving its current field output.
- `projects/planet` uses shared deterministic 3D CPU and GLSL FBM for its
  terrain field while keeping terrain shaping constants project-owned.
- `projects/terrain_lab`, `projects/ocean`, and `projects/fluid/water_2d`
  consume shared GLSL value-noise helpers for matching shader breakup/noise.
- `projects/fluid/water_2d` and `projects/fluid/water_3d` consume shared GLSL
  uint hash-to-unit helpers for particle spawn jitter, emission randomness, and
  deterministic transfer tie-breaks.
- Shared atmosphere/sky shaders consume shared GLSL PCG hash and 2D value-noise
  helpers for procedural stars and moon terminator breakup while keeping the
  star population, moon lighting, and visibility recipes domain-owned.
- `projects/cloud` uses the shared GLSL remap primitive in the active cloud
  common shaders while leaving source-aligned Perlin/Worley recipes intact.

## Migration Tiers

Procedural unification should move in tiers instead of forcing every project
through one noise API:

- CPU primitives live in `cubey::procedural`: deterministic hashes, value noise,
  coherent noise wrappers, scalar fields, and renderer-independent operators.
- Shader primitives live under `shaders/cubey/procedural`: small GLSL helpers for
  repeated hash/random, value-noise, FBM, remap, and smoothing formulas.
- Domain drivers remain project or domain code: terrain ridges/rivers/dunes,
  cloud volume texture recipes, ocean foam breakup, fluid turbulence, lunar
  atlas features, and star placement should consume shared primitives only when
  the formulas match their current behavior.

The first migration wave is a preserve-output refactor. It removes duplicated
helper formulas where they already match, but it does not silently change
screenshots, default Terrain Lab noise sources, active cloud volume generation,
or planet terrain shaping constants.

Next shared candidates should come from repeated project-local code or from a
specific reference-backed driver need, not from speculative API surface. Likely
near-term candidates are:

- reusable slope/curvature and local-relief operators;
- terrain-oriented ridge/terrace/remap composition helpers;
- flow-routing and accumulation data structures after a deeper SimpleHydrology
  pass;
- explicit source-field recipes for mountain range, river, and dune drivers.

Near-term non-goals:

- no full procedural node graph;
- no default switch from legacy Terrain Lab value noise to FastNoiseLite;
- no FastNoiseLite GLSL migration before shader parity has its own focused
  review;
- no CPU migration of generated atmosphere lunar/night-sky atlas noise until it
  has its own golden-value or image-review pass;
- no deduplication of `cloud_ref`, `cloud_ref_2`, or `clouds_legacy` while they
  are still useful as reference snapshots;
- no promotion of physical hydrology, erosion, foliage, materials, streaming, or
  Vulkan resource policy into `cubey::procedural`.

## Adoption Rule

Promote a helper from a project into `cubey::procedural` only when it is:

- deterministic and testable without a renderer;
- useful to at least two projects or clearly foundational for one project and an
  integration target;
- named by terrain/process meaning rather than by one slice's visual outcome;
- documented with limits so downstream projects do not mistake an approximate
  driver for a physical simulation.
