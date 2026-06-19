# Procedural Generation Foundation

Cubey uses procedural assets across terrain, planet detail, ocean-adjacent
fields, fluids, clouds, atmosphere backdrops, generated textures, and validation
scenes. Terrain Lab and `projects/procedural_terrain` have made the shared
problem clearer, but they are experiments rather than API owners. Procedural
work should start from coherent source fields and reusable operators, not from
slice-local hand-authored masks that are later dressed with materials or shader
noise.

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
materials, foliage, or Vulkan resources. Early terrain project contracts can be
deprecated or rebooted when they conflict with the cleaner foundation model.

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

## Experimental Terrain Lessons

Terrain Lab and `projects/procedural_terrain` are now legacy/reference
experiments. They remain useful evidence for what worked, what failed, and what
downstream terrain or shoreline consumers may need, but new foundation work
should not migrate further toward their current contracts. The planned terrain
reboot can consume the shared foundation later without inheriting either
project's early payload shape. The current rule is:

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

A broader local reference pass over `~/code/ref/3DWorld`,
`~/code/ref/Planet-Generator`, `~/code/ref/TerraForge3D`, and
`~/code/ref/terrain-diffusion` is captured in
`docs/notes/procedural-terrain-reference-review.md`. The stable conclusions are:

- borrow code-centric concepts, not UI node graphs or whole engines;
- treat procedural results as named field sets: height, masks, process fields,
  material hints, climate or environment drivers, diagnostics, and summaries;
- represent source construction as named layer stacks with masks, weights,
  domain transforms, warp, distribution shaping, and stats;
- make random-access tile or patch sampling part of the deterministic contract;
- keep ML terrain generation out of the runtime foundation for now, while
  borrowing its conditioning, climate-field, and quantile-remap ideas.

## Current Foundation Slice

The first shared CPU-side layer lives under `include/cubey/procedural` and is
compiled into `cubey::core`:

- `Grid2DDesc` and `ScalarField2D` provide local 2D scalar-field storage,
  centered sample coordinates, bounds-checked indexing, and field summaries.
- `FieldSet2D` groups named scalar fields over one grid descriptor for generic
  debug, process, climate/environment, terrain, cloud, and generated-texture
  payloads.
- `operators.h` provides scalar helpers (`saturate`, `lerp`, `smoothstep`,
  `smootherstep01`) plus field operators for blur, normalization,
  slope/curvature, local relief, remap/clamp, signed/unit conversion, power and
  terrace shaping, ridge shaping, field composition, percentile summaries, and
  percentile remapping.
- `noise.h` provides Cubey-wrapped FastNoiseLite coherent noise/domain-warp
  sampling plus deterministic legacy 2D and 3D hash/value-noise, FBM, and
  ridged FBM with explicit octave/lacunarity/gain/seed-stride configuration.
- `seed.h` provides fixed string hashing, named-domain seed derivation, and a
  named-domain `random01` helper so projects can split procedural streams
  without scattering magic seed offsets.
- `sample_domain.h` provides semantic 2D and 3D sample descriptors for local,
  world, unit, atlas, volume, and spherical data. `SampleDomain2D` wraps
  `Grid2DDesc` with seed and domain-space metadata; `SampleDomain3D` adds
  centered volume coordinates and bounds-checked indexing.
- `artifact_metadata.h` provides generated artifact identity, value format,
  semantic domain, seed, extent, mip/face layout, and content-hash metadata for
  in-memory comparison of procedural outputs.
- `source_fields.h` wraps those samplers as deterministic 2D source fields and
  layered `SourceRecipe2D` stacks that can fill `ScalarField2D` grids, carry
  debug fields, apply masks/weights/blend modes, and optionally normalize final
  output.
- `shaders/cubey/procedural` mirrors the small GLSL side of this layer with
  shared remap/smoothing helpers, deterministic random/hash helpers, and
  value-noise/FBM helpers for formulas that already match existing project code.
  The first shader parity pass records exact CPU/GLSL contracts for scalar
  shaping, uint hashing, masked hash-to-unit conversion, and legacy 3D
  value-noise/FBM helpers, while keeping PCG and sin-dot shader helpers as
  shader-only visual formulas for now.

Terrain Lab consumes this shared layer for its scalar helpers and deterministic
FBM source. It also exposes opt-in FastNoiseLite and warped FastNoiseLite
backends for the desert dune source driver. That adoption is now a preserved
experiment rather than a migration template: default captures keep the legacy
backend, the coherent noise paths can still be inspected explicitly, and the
diagnostic adapter from `TerrainLabFieldData` into `FieldSet2D` remains useful
evidence without freezing Terrain Lab's field layout.

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

The first migration wave is mostly a preserve-output refactor. It removes
duplicated helper formulas where they already match, but it does not silently
change default Terrain Lab noise sources, active cloud volume generation, or
planet terrain shaping constants.

The first promoted field-analysis operators are reusable slope/curvature and
local-relief scans for `ScalarField2D`. Terrain Lab now consumes those operators
for terrain derivatives and for mountain/glacial local relief analysis. The
mountain slice uses that shared analysis as the proof case for broad uplift,
ridge, peak, cliff, and scree source fields; the glacial slice keeps a separate
valley/process source shape and uses the shared operators only for wall and
process cues.

The source-field construction batches add scalar-field composition,
deterministic 2D noise-source sampling, optional coherent source warping, and
common unit-field shaping. The goal is not a node graph; it is a small C++
reference layer that can sample deterministic 2D noise fields, remap/blend/shape
them, and make Terrain Lab's source formulas easier to inspect and later share.
The first proof consumer is the desert dune source selection, preserving the
legacy default while making FastNoiseLite and warped FastNoiseLite explicit
opt-ins.

The first broader foundation batch promotes generic named field sets,
distribution/percentile operators, and layered `SourceRecipe2D` composition.
These are intentionally domain-neutral: Terrain Lab can adapt its current height
and driver fields into them, but terrain-specific landform drivers, hydrology,
materials, and foliage remain outside core.

The non-terrain foundation batches promote named seed domains, semantic sample
domains, and generated artifact metadata after reviewing active atmosphere,
cloud, ocean, fluid, planet, and future terrain needs. This adds reusable
address/metadata vocabulary without changing project formulas: atmosphere
atlases, cloud density/weather volumes, ocean foam/detail, fluid
jitter/turbulence, and future terrain tiles can share seed, domain, and artifact
identity language while keeping their domain recipes project-owned. The first
metadata consumers are the generated atmosphere lunar/night-sky atlas pair and
the production cloud generated texture catalog. Atmosphere atlases now carry
in-memory metadata and content hashes while preserving atlas bytes. Cloud now
exposes descriptor metadata for its GPU-generated base density volume, detail
erosion volume, and weather map while leaving `content_hash = 0` until a
readback/export path exists.

Next shared candidates should come from repeated project-local code, broad
procedural-rendering needs, or a specific reference-backed driver need, not from
speculative API surface.

The foundation closure batch should close the low-risk contracts already implied
by the current CPU-side layer:

- field-set export metadata so old and new procedural field outputs are easy to
  compare by generator identity, formula version, seed, semantic domain,
  dimensions, and content hash;
- deterministic tile or patch descriptors so future terrain and planet work can
  sample local patches without inventing incompatible address and border
  semantics.

Remaining candidates after that closure batch are:

- GPU-executed shader golden tests or FastNoiseLite GLSL parity before any
  future shader-side coherent-noise migration;
- cloud GPU readback or export metadata so generated-volume and weather-map
  descriptors can report content hashes instead of descriptor-only identity;
- a future `SourceRecipe3D` or volume-field variant after the cloud/environment
  review proves the shape;
- flow-routing and accumulation data structures after a deeper SimpleHydrology
  pass;
- explicit source-field recipes for mountain range, river, and dune drivers.

Near-term non-goals:

- no full procedural node graph;
- no compatibility promise for early Terrain Lab or `procedural_terrain`
  payloads;
- no further Terrain Lab or `procedural_terrain` migration as a foundation
  milestone before the terrain reboot;
- no migration of atmosphere, cloud, ocean, or fluid visual formulas in the seed
  and sample-domain batch;
- no JSON sidecar or file export format for generated artifact metadata until
  more than the atmosphere atlas consumer needs persistent metadata;
- no default switch from legacy Terrain Lab value noise to FastNoiseLite;
- no FastNoiseLite GLSL migration before a dedicated GPU or shader-side parity
  pass;
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
