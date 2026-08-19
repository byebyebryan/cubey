# Terrain Reference Study

`studies/terrain/reference` is the frozen, maintenance-only visual benchmark for
terrain. It is not the production terrain system and it does not inherit the
old river/mountain workbench pipeline. Do not add recipes or evolve its product
contract; new terrain work belongs in `projects/terrain`. The benchmark carries
these reference recipes over a Cubey clipmap review mesh:

- `terrain-engine-ref`: a TerrainEngine-inspired shader-side height sampler and
  procedural material presentation.
- `shadertoy-mountain`: a clean-room ShaderToy-style mountain heightfield
  sampler with a matching procedural material response.
- `shadertoy-erosion-filter`: a clean-room slope-aware procedural erosion
  filter over a derivative-aware broad mountain source, with selectable base
  and filtered surfaces plus a signed erosion diagnostic.
- `shadertoy-alpine`: a clean-room alpine range reference with broad mountain
  mass, ridged crests, valley suppression, snow, rock, and meadow bands.
- `shadertoy-dunes`: a clean-room desert dune reference with wind-aligned
  rolling ridges and procedural sand/ripple material response.
- `shadertoy-lake-basin`: a clean-room lake-basin reference with surrounding
  hills, a warped basin depression, waterline intersection, and shoreline cues.
- `shadertoy-badlands`: a clean-room arid badlands reference with plateau mass,
  dry washes, eroded cuts, cliff exposure, and strata-like material cues.
- `shadertoy-coast-island`: a clean-room coast/island reference with noisy
  shoreline, beach shelf, inland hill buildup, local coastal cliffs, and fixed
  sea-level contact.
- `shadertoy-plains`: a clean-room low-relief grassland reference with rolling
  broad terrain, shallow swales, and wind/grass material variation.
- `shadertoy-gorge`: a clean-room dry gorge reference with a warped incision
  corridor, tributary cuts, arid floors, cliff exposure, and strata cues.
- `shadertoy-glacial-highland`: a clean-room icy highland reference with broad
  snow/ice fields, U-shaped valley hints, rock ribs, and talus/ice contrast.
- `shadertoy-crater-field`: a clean-room cratered terrain reference with
  overlapping depressions, raised rims, ejecta roughness, and barren regolith
  material.

The current target is useful for renderer and source-model evaluation:

- coherent world-space height sampled in GLSL;
- procedural slope/elevation material zoning;
- neutral height material mode for source-shape comparison;
- distance-faded procedural albedo and normal detail;
- directional lighting and lightweight distance/altitude fog;
- optional flat waterline intersection;
- oblique and surface review cameras;
- deterministic source/config and mesh checks for comparison. The former viewer
  and capture scripts are retired.

For `shadertoy-erosion-filter`, use `--terrain-preview-surface pre-process` to
review the base source and `height` or `post-erosion` to review the filtered
surface. `--terrain-preview-color erosion` shows signed height removal. This is
a stateless procedural filter, not hydraulic erosion.

Other recipes remain unfiltered by default. Passing
`--terrain-preview-surface post-erosion` explicitly applies the same filter to
their source height and numerically estimated gradient; `pre-process` keeps the
source untouched. This is a cross-biome evaluation mode, not a universal biome
process policy.

Known limitations are intentional for this slice: the review mesh is finite,
water is still a tint/clamp rather than reflection/refraction, material detail
is shader-generated rather than authored texture data, the ShaderToy-inspired
recipe samples a heightfield rather than porting a raymarch renderer, and there
is no biome, hydrology, foliage, streaming, or planet-scale paging. The
ShaderToy recipes are visual/source references, not production biome contracts.

## Commands

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies --target \
  cubey_study_terrain_reference_core cubey_study_terrain_reference_tests
ctest --preset dev-terrain-studies -R '^terrain_ref_tests$' --output-on-failure
```

The former windowed/headless viewer and its capture scripts were retired. The
reference shaders, typed configuration model, CPU samplers, mesh construction,
and historical capture evidence remain available for source-level comparison.

The closure status, multi-seed matrix, carry-forward decisions, and known weak
recipes are recorded in
[`docs/notes/terrain-ref-closure.md`](../../../docs/notes/terrain-ref-closure.md).
Further feature and rendering work is intentionally deferred to the terrain
reboot and its consumers.
