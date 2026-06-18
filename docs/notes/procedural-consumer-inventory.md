# Procedural Consumer Inventory

Date: 2026-06-18

This note captures the non-terrain procedural consumers that should shape the
next shared foundation work. `terrain_lab` and `projects/procedural_terrain`
remain useful legacy evidence, and the planned terrain reboot is still expected
to become the largest consumer later, but near-term foundation APIs should be
validated against the active rendering and simulation projects too.

## Current Consumers

### Atmosphere And Environment

Relevant consumers:

- generated lunar atlas and night-sky atlas code under `src/cubey/render`;
- shared atmosphere shaders for procedural stars and moon terminator breakup;
- atmosphere configuration tests that already protect atlas determinism and
  shader-helper adoption.

Shared needs:

- stable seed derivation for atlas sub-features such as star cells, Milky Way
  structure, crater/terminator breakup, and deterministic capture variants;
- atlas-oriented 2D sample domains that are not confused with terrain grids;
- generated asset metadata so atlas revisions can be compared by seed, domain,
  dimensions, and formula version.

Do not migrate generated atmosphere atlas noise formulas in the foundation
batch without a focused golden-value or image-review pass.

### Cloud

Relevant consumers:

- generated 3D Perlin-Worley base volume, Worley detail volume, and 2D weather
  maps in `projects/cloud`;
- local and orbit weather coverage/detail/hull procedural fields;
- static sampling controls and deterministic jitter for ray starts.

Shared needs:

- seeded 3D/volume sample domains with explicit dimensions, cell spacing,
  origin, and semantic space;
- stable named seed domains for base density, detail erosion, weather maps,
  orbit coverage/detail, and sampling jitter;
- future `SourceRecipe3D` or volume-field recipes after the cloud volume shapes
  are reviewed against the current renderer.

Do not deduplicate `cloud_ref`, `cloud_ref_2`, or `clouds_legacy`; those remain
reference snapshots while production cloud keeps its active density model.

### Ocean

Relevant consumers:

- spectral-domain wave generation and cascade slots in `projects/ocean`;
- shader procedural helpers for foam breakup, terrain-field diagnostics, and
  surface detail isolation;
- future inputs for bathymetry, shoreline masks, local disturbances, and cloud
  shadows.

Shared needs:

- stable named random streams for opt-in visual breakup and diagnostics;
- surface/texture domain descriptors that can name local tangent space,
  diagnostic terrain-field space, and future shoreline/bathymetry space;
- generated field metadata for comparing shoreline and foam inputs once real
  terrain integration returns.

Ocean should stay a consumer of terrain, bathymetry, weather, and cloud outputs;
it should not own those generators in the shared foundation.

### Fluid And Pyro

Relevant consumers:

- `water_2d` particle jitter, hose emission randomness, transfer tie-breakers,
  foam breakup, procedural caustics, and offscreen density surfaces;
- `water_3d` particle/grid randomness and deterministic transfer helpers;
- `smoke_2d`, `fire_3d`, `explosion_3d`, and `pyro_3d` turbulence, volume,
  density, and shadow-volume data paths.

Shared needs:

- deterministic seed derivation for independent emitter, particle, transfer,
  turbulence, and visualization streams;
- sample domains that can describe local 2D surfaces and 3D solver volumes
  without importing renderer or Vulkan policy;
- shader parity for simple hash/random helpers before higher-level turbulence
  recipes are promoted.

Simulation policy, solver state, and Vulkan resource ownership should remain in
the fluid projects.

### Planet And Future Terrain

Relevant consumers:

- planet surface detail and local/global LOD as an integration target;
- future proper terrain project as the likely largest user of field sets,
  domain descriptors, source recipes, hydrology, and generated metadata.

Shared needs:

- deterministic tile/patch semantics that can scale from local workbench
  captures to planet patches;
- named field outputs and summaries for height, process, climate, material,
  water, and diagnostics;
- enough seed/domain discipline that terrain can add mountains, rivers, dunes,
  and climate fields without hand-authored slice masks.

The current terrain projects should not be further migrated to define this
contract. They are reference snapshots until the terrain reboot starts.

## Next Foundation Shape

The next code-level primitives should be small and renderer-independent:

- named seed derivation, so projects stop scattering magic offsets;
- 2D and 3D sample-domain descriptors, so generated assets can say whether a
  grid is local, world, atlas, volume, or spherical data;
- later generated asset metadata, shader parity checks, and volume/source
  recipes once the first two primitives are in place.

This is intentionally not a visual migration batch. Active projects should keep
their current formulas unless a focused follow-up proves parity or a better
domain recipe.
