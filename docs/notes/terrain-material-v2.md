# Terrain Material V2

Date: 2026-07-20

Status: candidate complete; recommended for acceptance, visual review pending.

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

## Candidate Result

The retained matched pack was captured at `1600 x 900` under
`outputs/terrain/material-v2/`. The control uses revision `bf91478e`; the final
candidate uses revision `24e008f3`. Finalization confirms identical frozen
inputs and products:

- elevation SHA-256:
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- product content hash: `0xcf2100b0763a8211`;
- `2,657,280` cached source samples;
- render stride 3 and `607,200` triangles;
- one `5,592,404` byte generated material texture;
- zero difference in the matched `flat` control.

| Lane | Clear mean | Clear p50 | Clear p95 |
|---|---:|---:|---:|
| control | 0.931 ms | 0.920 ms | 0.943 ms |
| candidate | 0.942 ms | 0.911 ms | 0.936 ms |

The candidate stays below the `1.10 ms` mean and p50 gate. Mean increases by
`0.011 ms`, while p50 and p95 are slightly lower; this is measurement-scale
variation rather than a material cost regression. The shader still performs
two planar samples and, for rock-dominant fragments only, two additional
triplanar samples.

The qualified comparison shows broader warm/cool mineral identity and clearer
soil-versus-rock separation without changing terrain shape. The gain is
strongest in the mountain-heavy 0 and 90 degree headings and remains restrained
in the sparse 180 and 270 degree headings. The normal diagnostic replaces the
uniform fine-grain field with wider terrain-scale variation. Raking light no
longer relies on dense micro-noise to reveal the surface, and the fair-cloud
frames retain the same environment composition.

No periodic tile, material seam, grass implication, snow blanket, or glossy
response is visible in the retained pack. The 100 m stress views are less noisy
but still too bare for a close-terrain claim. Raking light continues to expose
terraced shoulders and stepped source transitions; those are unchanged source
or cached-product limitations and should not be hidden with stronger material
noise.

The candidate is recommended as the bounded Material V2 far-backdrop baseline.
It does not justify another immediate material-tuning loop. After visual review,
either merge it and exercise the accepted backdrop in one real consumer, or
return explicitly to source filtering if the remaining terracing fails the
qualified far-field use case.

## Validation Result

The complete default build, including all maintained shaders and projects,
passes. All six focused terrain tests pass, followed by all `141 / 141` default
CTest cases in `798.47 s`. The branch also passes the matched capture
invariants, profile budget, shader compilation, and `git diff --check`.
