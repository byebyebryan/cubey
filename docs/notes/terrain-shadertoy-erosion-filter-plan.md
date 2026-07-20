# Terrain ShaderToy Erosion Filter Reference

Date: 2026-07-09

This note defines one final `terrain_ref` experiment before the reference lane
is closed. The target is the compact slope-aware erosion-filter family in the
local ShaderToy references, especially:

- `~/code/ref/ShaderToy/clean_terrain_erosion_filter_*`;
- `~/code/ref/ShaderToy/advanced_terrain_erosion_filter_*`;
- `~/code/ref/ShaderToy/terrain_erosion_noise_*`.

## Decision

Add one isolated recipe named `shadertoy-erosion-filter`. It should reproduce
the useful source-model idea clean-room: start from a derivative-aware broad
mountain field, align procedural gullies with the downhill slope, and let each
gully octave update the slope used by the next octave.

This is a procedural erosion filter, not hydraulic erosion. It has no rainfall,
water depth, flux, velocity, sediment capacity, sediment transport, deposition,
evaporation, or retained simulation state. The point is to test whether a
stateless random-access operator can add convincing branching erosion structure
without giving up the runtime properties that made the ShaderToy and
TerrainEngine references useful.

## Reference Boundary

- Keep the implementation clean-room. The reference family has mixed ancestry
  and the advanced filter is MPL-2.0.
- Port the terrain and erosion model, not the raymarch renderer, atmosphere,
  water, materials, keyboard state, or comparison animation.
- Keep all formulas in `studies/terrain/reference`. Do not promote an experimental
  terrain process into `cubey::procedural`.
- Use world meters for horizontal position, height, gradients, and erosion
  scale. The broad mountain wavelength should establish the gully scale.
- Preserve correct height derivatives through every octave. The branching
  mechanism depends on each octave changing the downhill direction seen by the
  next.

## Review Contract

The recipe exposes one base source and one filtered result:

- `--terrain-preview-surface pre-process` selects the unfiltered base height;
- `--terrain-preview-surface height` and `post-erosion` select the filtered
  height;
- `--terrain-preview-color erosion` displays signed height removal;
- material and height views continue to use the existing terrain renderer.

The recipe is waterless. Its material response should reuse the existing
mountain reference presentation so the comparison is about geometry rather
than a new renderer.

## Acceptance

- The filtered surface keeps the same broad mountain silhouette as the base.
- Gullies follow slopes and branch without an obvious cell lattice or repeated
  stripe direction.
- Flat areas, broad summits, and valley floors remain quieter than steep flanks.
- The effect reads in geometry from both oblique and surface-low cameras, not
  only in material color.
- Fixed seed and coordinates are deterministic; different seeds change the
  gully placement.
- Height and returned gradients remain finite, bounded, and consistent with
  finite-difference checks.
- Default windowed rendering remains usable; capture timing is recorded but is
  not yet a production performance gate.

## Deferred

- Applying the filter to `shadertoy-alpine` or any other existing recipe.
- Advanced ridge/crease rounding, drainage maps, and interactive controls.
- A stateful hydraulic erosion or sediment simulation.
- Tile-border policy, regional process storage, and production terrain
  promotion.

## Review Outcome

The isolated reference is complete and useful enough to retain. Compared with
the deliberately smooth base field, the filtered surface adds branching,
slope-following gullies while preserving the broad mountain silhouette. The
change remains visible in the surface-low material and height captures, so it
is a geometric result rather than a shading-only treatment. The four-seed
stress pack changes both the macro field and gully arrangement without exposing
a fixed centered corridor or obvious cell lattice.

The diagnostic uses orange/red for positive removal, blue for local negative
removal, and a dark neutral near zero. Both signs are expected: the procedural
filter is displacement with a lowering bias, not a mass-conserving erosion
solver. Dense fine striations can still make steep flanks read as busy or hairy,
and the approximate process gradients do not represent physical water flow.
Those limitations must stay explicit if this operator is reused.

Review artifacts are generated under
`outputs/terrain_ref/shadertoy-erosion-filter/`:

- `comparison-contact-sheet.png` compares base, filtered, and erosion views
  from the oblique and surface-low cameras;
- `seed-stress-contact-sheet.png` compares filtered geometry and erosion
  diagnostics for seeds 0, 42, 9012, and 12345;
- `capture-summary.txt` records a full 1280x720 pack time of 11 seconds on the
  review machine.

The result validates a stateless procedural erosion operator as a candidate
process for a future production terrain source/process pipeline. It does not
validate hydraulic erosion, drainage continuity, sediment behavior, tile
boundaries, or production-scale performance. Keep it reference-local and do
not transfer it to the alpine recipe until that work is chosen explicitly.
