# Terrain And Foundation Closure

Date: 2026-07-22

Status: validated and closed.

## Scope

This checkpoint closes the persistence and progressive-initialization work
needed by Terrain V1 without broadening the product into streaming, close
terrain, or procedural terrain generation.

Three data classes remain intentionally distinct:

| Data | Owner | Persistence |
| --- | --- | --- |
| Terrain Diffusion elevation and climate bundles | External source producer plus `cubey::asset` validation | `cache/terrain/sources/v1/` |
| Terrain Diffusion checkout, Python environment, and downloaded inputs | Explicit developer tooling | `cache/terrain/tooling/v1/` |
| Derived terrain products and atmosphere atlases | Shared procedural cache adapters | `cache/procedural/v1/` |

All three roots are Git-ignored and worktree-local. Build directories and
`clean` targets do not own the terrain source or tooling caches.

## Source Integrity

The source-reuse gate now checks the complete manifest and payload contract
before skipping an expensive producer run:

- pinned generator, code, and model provenance;
- seed, grid dimensions, spacing, origin, and axis mapping;
- height transform, payload shape/layout/type/unit, and exact byte count;
- climate heightfield binding, ordered channels, and physical ranges;
- finite float values; and
- SHA-256 of the actual elevation or climate bytes.

The C++ height and climate loaders enforce the same payload identity boundary.
Only a verified climate digest can contribute to the derived terrain-product
recipe, so same-size payload replacement cannot reuse a stale product.

The dependency-free source-cache tests are registered with CTest. The larger
NumPy-backed producer suite remains an explicit tooling test and is not needed
for ordinary configure/build/test.

## Derived Products

Terrain's compact sector product remains recipe-keyed by verified source
identity, placement, topology, render stride, surface model, and codec version.
Windowed terrain prepares source loading, placement, cache lookup or generation,
and GPU installation through one staged path. A complete generation activates
at the update boundary while the previous generation remains valid through its
last submission ticket. Headless capture finishes the same request before frame
zero.

Deleting `cache/procedural/v1/terrain.backdrop.product/` rebuilds only the
derived terrain product. Deleting `cache/terrain/sources/v1/` is a separate,
deliberate source-generation decision.

## Atmosphere Atlas Adoption

`cubey::AtmosphereBackgroundAtlasRuntime` is the reusable placeholder-first
boundary for fixed atmosphere atlases:

```text
placeholder GPU pair
    -> cached CPU preparation job
    -> complete GPU upload
    -> app-boundary descriptor activation
    -> submission-ticket retirement
```

Terrain and glTF Viewer use this runtime. glTF refreshes both its visible
atmosphere background and reflection-probe descriptors when the real atlases
activate. Their headless paths block on the same staged request for deterministic
captures.

The standalone atmosphere app keeps its specialized dynamic-atlas staging.
Ocean, planet, water-3D, and pyro-3D retain synchronous resource ownership but
now use the same persistent atlas cache, removing repeat CPU generation without
forcing a swapchain-lifecycle refactor into this checkpoint.

## Operational Commands

Generate or validate the canonical source explicitly:

```sh
cmake --build --preset dev --target cubey_terrain_generate_default_asset
```

Force only derived-product regeneration by removing its procedural-cache
subdirectory. Force external source regeneration with the explicit terrain
regeneration target; ordinary application startup never downloads a model or
runs inference.

The obsolete `outputs/terrain/.terrain-diffusion-venv` location is retired.
The wrapper, CMake cache variables, and default data-cache argument all resolve
under `cache/terrain/tooling/v1/`.

## Remaining Boundaries

- Terrain source loading is job-backed but not tiled or streamed.
- GPU installation is complete-generation upload, not partial residency or a
  per-frame transfer budget.
- Older atmosphere consumers are cache-backed but not placeholder-first.
- glTF asset decoding and general scene-resource dependency scheduling remain
  separate work.
- Hydrology, terrain generation, climate/biome semantics, foliage, and
  close-terrain rendering remain outside this foundation closure.

## Closure Gate

The closure gate was:

1. Debug and Release builds for all default targets.
2. Full serial CTest.
3. Dependency-free and NumPy-backed terrain tooling tests.
4. Real default-source reuse without model initialization.
5. Cold/warm terrain and glTF captures with byte-identical output.
6. `git diff --check`, clean worktree, and publication of `main`.

## Validation Result

The 2026-07-22 closure run passed:

- complete Debug and Release builds;
- all 150 serial CTest cases;
- both dependency-free source-cache cases and all 20 NumPy-backed producer
  cases;
- reuse of `cache/terrain/sources/v1/default` without producer startup; and
- no remaining project or test call sites for uncached atmosphere-atlas
  generation.

Release cold/warm captures were run with the existing procedural cache moved
aside, then the original cache was restored:

| Consumer | Cold wall time | Warm wall time | PNG SHA-256 |
| --- | ---: | ---: | --- |
| Terrain | 1.36 s | 0.37 s | `424766c3b3626d374549b917f2c651a8bf259f50613f1cee131a543770d7415b` |
| glTF Viewer with terrain | 1.73 s | 0.94 s | `a020224ab72220812886210f99d84c9f1f28672c2c27cbe45d23fd49d938cfe7` |

Each consumer produced the same hash in its cold and warm runs. Review captures
are retained under `outputs/terrain/foundation-closure-2026-07-22/`.
