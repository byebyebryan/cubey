# Terrain Material V2

Date: 2026-07-20

Status: implementation candidate; visual verdict pending.

## Goal

Improve the accepted far-backdrop terrain through broad mineral identity,
mesoscopic surface response, and clearer terrain-light separation. The batch
must make the existing macro form read more convincingly without changing the
height source, selected placement, cached geometry, topology, silhouette,
directional-shadow system, shared atmosphere, or supported camera envelope.

This remains a far-field backdrop. It does not add close terrain, traversal,
foliage, imported textures, hydrology, displacement, streaming, adaptive LOD,
or a reusable engine terrain API.

## Frozen Control

The comparison control is the retained rendering-envelope V1 product:

- elevation SHA-256:
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- selected source focus: `8500 / -2500 m`;
- selected product content hash: `0xcf2100b0763a8211`;
- cached source samples: `2,657,280`;
- complete render product: `607,200` triangles at render stride 3;
- continuous seam-matched center, 48 sectors, and `16.384 km` outer radius;
- qualified 500 m foreground focus, 50-250 m orbit radius, 0-30 degree orbit
  elevation, unrestricted yaw, and 40 degree vertical field of view.

The 100 m foreground focus remains a diagnostic stress view. It does not
expand the product support claim.

## Reference Findings

TerrainEngine's useful material lesson is structural rather than literal. Its
terrain reads through broad sand, grass, and rock classes, slope-driven rock
exposure, a separate normal signal, and distance fog. Its implementation also
depends on imported color and normal textures, simplified direct lighting, and
a grass color that implies surface detail it does not render. Those assets and
tradeoffs are not part of this product.

Material V2 keeps Cubey's generated texture and physical environment, while
borrowing the scale hierarchy:

1. broad, low-saturation mineral regions identify large surfaces;
2. slope and existing material weights expose rock coherently;
3. mesoscopic normals improve raking-light response;
4. local variation breaks uniformity without becoming visible noise.

The ShaderToy terrain studies reinforce the same boundary: their convincing
far-field results rely on disciplined frequency allocation and lighting, not a
uniform stack of high-frequency detail.

## Material Contract

Retain the public `flat` and `filtered-detail` presentations. `flat` remains a
pixel-stable geometry and lighting control. Material V2 replaces only the
private generated content and shader interpretation behind `filtered-detail`.

The generated resource remains one deterministic `1024 x 1024` RGBA8 texture
with 11 mips, the existing descriptor layout, and a `5,592,404` byte
allocation. Its private seed domain advances to
`terrain.backdrop.filtered-detail.v3`.

Channel use remains compact:

- RG stores a tangent normal derived from the 512 m, 186 m, 71 m, and a
  restrained 29 m relief band;
- B is interpreted as a broad mineral selector in the macro sample and only
  restrained tonal breakup in the local sample;
- A provides broad roughness variation with weaker local modulation.

The 11 m and 4.4 m bands do not contribute to the generated normal. Ground and
snow remain planar. Only rock-dominant fragments use triplanar projection, and
the complete material remains at the current maximum of four generated-texture
samples per fragment.

The palette is mineral-led: cool neutral stone, restrained warm stone, and
desaturated soil. Snow remains sparse and cool. There is no grass-green
implication, imported image texture, strata banding, gloss, or displacement.
The existing rock, snow, ground, height, slope, and ambient-visibility inputs
remain unchanged; only their presentation is remapped.

## Review Matrix

The ignored matched pack lives under `outputs/terrain/material-v2/`. Both
lanes use the same executable contract, heightfield, selected placement,
stride 3, paused 09:00 solar environment on day 172 at 35 degrees latitude,
terrain shadows, and `1600 x 900` output.

- qualified surface: headings 0, 90, 180, and 270 degrees at a 500 m focus,
  100 m orbit, and 8 degree elevation;
- raking light: headings 90 and 180 degrees with 12 degree sun elevation and
  35 degree azimuth;
- stress control: headings 90 and 180 degrees at a 100 m focus;
- composition: fair-cloud frames at headings 90 and 180 degrees;
- diagnostics: matched material albedo, normal, roughness, and material-weight
  frames at headings 90 and 180 degrees;
- profile: clear stride-3 composition after 30 warmup and 120 measured frames.

Control is captured before the material shaders change. Candidate capture must
refuse to finalize when elevation hash, product hash, source-sample count,
render stride, triangle count, or material allocation differs between lanes.

## Acceptance Gates

Material V2 is accepted only when qualified views show clearer broad and
mesoscopic surface separation in at least three of four headings, with no
visible regression in the fourth. Raking-light views should reveal slopes and
rock masses rather than a uniform fine normal field. The final surface must not
show periodic tiles, contour bands, high-frequency noise blankets, false grass,
snow blankets, gloss, or material seams.

The `flat` control must remain pixel-identical. Source and product hashes,
cached samples, triangle count, render stride, material dimensions, allocation,
descriptor layout, and maximum texture samples must remain unchanged.

At `1600 x 900`, clear stride-3 atmosphere, terrain, stage proxy, and post must
remain at or below `1.10 ms` for both mean and p50, and neither value may regress
more than `0.10 ms` from the matched control. P95 and fair-cloud composition are
reported but do not gate this material batch.

Required mechanical validation is the terrain build, focused terrain and run
config tests, the complete default test suite, shader compilation, matched
headless captures, `git diff --check`, and a clean candidate worktree. The
candidate remains unmerged until visual review.
