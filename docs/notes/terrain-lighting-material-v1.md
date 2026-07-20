# Terrain Lighting And Material V1

Date: 2026-07-20

Status: implementation candidate; keep isolated until visual review.

## Goal

Improve the accepted far-backdrop terrain through terrain-scale directional
shadows and clearer procedural material scale separation. This batch must make
the existing macro form easier to read without changing the height source,
placement, cached geometry, topology, silhouette, or supported camera envelope.

This is still a far-field backdrop. It does not add hero terrain, traversal,
foliage, imported textures, hydrology, displacement, streaming, or a reusable
engine terrain API.

## Frozen Control

The comparison control is the canonical seed-0 Terrain Diffusion field:

- elevation SHA-256:
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- selected source focus: `8500 / -2500 m`;
- selected product content hash: `0xcf2100b0763a8211`;
- cached source samples: `2,657,280`;
- complete render product: `607,200` triangles at render stride 3;
- continuous seam-matched center, 48 sectors, and `16.384 km` outer radius;
- 100 m default foreground height, 50-250 m orbit radius, unrestricted yaw,
  0-30 degree elevation, and 40 degree vertical field of view.

Every shadow and material comparison must retain those values. Runtime
placement replacement may select a different valid product for diagnostics,
but the canonical evidence and pre/post geometry checks use the selected
control above.

## Directional Shadow Contract

Use the existing project-accessible `cubey::render::ShadowMapPass3D` rather
than introducing another renderer abstraction. The terrain app owns one
persistent `1024 x 1024` directional depth map and a terrain-specific depth
vertex shader.

The orthographic light camera covers the complete cached product bounds,
including the full `16.384 km` radius and product height span. It follows the
shared atmosphere primary light. The map is rendered on first use, swapchain or
resource recreation, successful placement-product replacement, and when the
primary-light direction changes by at least `0.25` degrees. Ordinary camera
orbit, yaw, zoom, and foreground changes do not update it.

When the primary light is below the terrain horizon, shadow rendering is
suspended and direct visibility is one. This avoids spending work on a light
whose direct terrain contribution is already absent. The terrain shader uses a
bounded `2 x 2` PCF lookup and a bias informed by receiver slope and product
depth span. The review must reject acne, detached shadows, sector seams,
camera-relative swimming, and visible map boundaries.

Public controls are `terrain.shadows`, `--terrain-shadows`, and
`--no-terrain-shadows`. Shadows default on for this candidate. The review UI
exposes one checkbox and reports map size, validity/update count, and the GPU
shadow pass. A `sun-visibility` diagnostic isolates the sampled result.

## Material Contract

Retain the public `flat` and `filtered-detail` presentations. `flat` remains
the geometry and lighting control. The candidate replaces only the generated
content and shader interpretation behind `filtered-detail`.

The material remains one deterministic `1024 x 1024` RGBA8 texture with its
existing mip chain and `5,592,404` byte allocation. Its seed domain advances to
`terrain.backdrop.filtered-detail.v2`. It provides two deliberate scales:

- one planar macro field over `32.768 km`, aligned to the terrain domain;
- one local detail field over `2.048 km` for bounded breakup.

Ground and snow use planar sampling. Only rock-dominant fragments may use
triplanar detail, and the complete material stays within five generated-texture
samples per fragment. Existing vertex height, slope, material weights, and
ambient visibility remain the semantic inputs. The palette stays neutral and
mineral-led, snow is restrained, and there is no green grass implication,
strata banding, imported image texture, displacement, or new vertex channel.

The current albedo, roughness, and normal amplitude ceilings remain upper
bounds. Improvement must come from semantic masks and scale separation rather
than stronger high-frequency noise.

## Evidence

The ignored review pack lives under
`outputs/terrain/lighting-material-v1/` and records runtime revision, source
provenance, product hash, geometry counts, configuration, and capture commands.
It contains:

- shadows off, shadows on, and `sun-visibility` under neutral and raking light;
- four matched headings for `flat`, `filtered-detail`, material albedo, and
  material normal;
- 100 m and 500 m foreground controls;
- explicit pre/post product hash, source-sample, and render-triangle checks.

Broad form must become clearer in flat/clay raking controls before material
detail is credited. The filtered presentation must add scale and surface
separation without hiding source defects, emphasizing low-poly silhouettes,
forming visible periodic tiles, or becoming a uniform noise blanket.

## Performance And Validation Gates

At `1600 x 900`, profile 30 warmup frames followed by 90 measured frames for
shadow-off, shadow-on-flat, and shadow-on-filtered lanes. The accepted selected
baseline is `0.855 ms` p50 for atmosphere, terrain, and post combined.

Candidate gates are:

- combined atmosphere, terrain, and post p50 no higher than `1.10 ms`;
- steady shadow/material cost below `0.25 ms` over the matched control;
- forced shadow-map update p50 below `0.50 ms`;
- no change to the frozen content hash or geometry counts.

Required mechanical validation includes focused shadow/config tests, default
and terrain-studies builds/tests, a headless PNG, one-frame Wayland smoke,
shader lint, `git diff --check`, and a clean candidate branch. The branch stays
unmerged until the generated evidence receives visual review.
