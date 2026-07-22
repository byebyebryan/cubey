# Terrain Rendering Acceptance V1

Date: 2026-07-21

Status: implementation plan and acceptance contract.

## Goal

Move the current Terrain Diffusion-backed terrain from a viable product path to
a rendering-acceptance candidate. The batch addresses three independently
observed problems: radial lighting facets in the continuous center mesh, coarse
directional-shadow sampling, and overly bright alpine snow under night
exposure.

This is a terrain-local rendering pass. It does not change the height or
climate sources, selected-placement contract, stage radius, source focus,
outer-product topology, shared atmosphere exposure, or shared PBR convention.

## Framing Contract

Use three foreground heights throughout the review:

- `100 m` is the explicit close stress case;
- `200 m` is the product default;
- `500 m` is the far-field comparison.

The 200 m view must be free of visible radial fan or wedge lighting artifacts.
The 100 m stress view may still expose the product's detail limit, but it must
remain continuous and show a material improvement over the accepted V1
baseline. Existing 100 m captures remain historical evidence rather than being
rewritten as the new default.

## Center Topology Contract

Retain the high-density seam-matched source sampling, angular render stride 3,
outer-terrain radial stride 3, 48 sectors, and `16.384 km` product radius. The
continuous center alone renders every sampled radial ring. Its center normal
comes from opposite first-ring source samples, and each later polar normal uses
an angular baseline matched to the local radial footprint. This avoids
degenerate near-center angular tangents without increasing the complete outer
backdrop density.

The expected candidate contains `201,696` center render triangles and
`742,368` total render triangles. Geometry and content hashes therefore change
intentionally. Source samples, source provenance, selected focus, outer
topology, and material allocation must remain unchanged.

## Shadow Contract

The terrain keeps one fixed `2048 x 2048` full-product directional shadow map.
It uses a `3 x 3` tent PCF kernel with separable `1 / 2 / 1` weights and the
receiver bias:

`max(0.75 m, texel_world * (0.25 + 0.35 * min(slope_tangent, 2)))`.

Retain the full cached-product projection, outer-sector caster ownership,
receiver-only center terrain, below-horizon suspension, and `0.25` degree
light-direction cache threshold. Do not add a public shadow-quality control.
The review rejects acne, detached shadows, block stepping, sector seams,
camera-relative swimming, and a visible map boundary.

## Snow Contract

The `flat` presentation remains unchanged. In `filtered-detail`, alpine snow
varies between weathered `(0.68, 0.72, 0.75)` and clean
`(0.77, 0.80, 0.82)` sRGB using the existing macro material field. Macro and
local albedo variation use `0.035` and `0.012`; snow ambient-occlusion strength
is `0.75`.

Snow remains non-emissive and highly rough. There is no time-of-day material
branch or night clamp. New `ambient-light` and `direct-light` diagnostics expose
the actual pre-aerial lighting contributions so night brightness can be
distinguished from emissive behavior.

## Evidence And Gates

Capture the cool/wet selected climate at `1600 x 900` for 100, 200, and 500 m
foreground heights and sun elevations `38`, `12`, `2`, `-6`, and `-18`
degrees. Include shadows off/on, sun visibility, classification normals,
projected edges, material channels, ambient light, and direct light. Include a
200 m daytime comparison of all five climate sources and a moving-clock
profile.

The clear default and moving-clock lanes must remain at or below `1.10 ms` mean
and p50 for atmosphere, shadow, terrain, and post combined, and no more than
`0.15 ms` above the matched control. Record moving-clock p95 and shadow-update
frequency as evidence, not as the primary gate. A two-hour-per-second
every-frame refresh lane records cache saturation but is not a product gate;
the accepted running clock uses the default `0.5` hour-per-second cadence.

Required validation is the complete terrain test set, shader compilation and
lint, the applicable default project tests, a headless capture, a runtime smoke
test, `git diff --check`, and a clean branch.
