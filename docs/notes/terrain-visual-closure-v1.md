# Terrain V1 Visual Closure

Date: 2026-07-23

Status: control defined; candidate pending.

## Goal

Close the current external-heightfield terrain as a convincing far-backdrop V1
without reopening source generation, placement, topology, LOD, or close-terrain
scope. The bounded work is the private generated mineral material and the
terrain-local interpretation of the accepted atmosphere lighting.

This batch does not add hydrology, foliage, biome synthesis, imported material
textures, close traversal, adaptive LOD, streaming, planet projection, or
terrain/ocean composition.

## Frozen Product

The control is the canonical default Terrain Diffusion heightfield under the
production `mineral-control` surface model:

- elevation SHA-256
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- deterministic selected placement, with raw center retained only as a control;
- continuous seam-matched center and 48 outer sectors;
- `16.384 km` outer radius and angular render stride 3;
- `742,368` complete render triangles;
- `200 m` default foreground focus, `500 m` qualified distance view, and
  `100 m` diagnostic stress view;
- shared atmosphere, clouds, HDR post, `2048 x 2048` terrain shadow map, and
  current shadow-cache policy.

Source samples, product hash, geometry, material allocation, descriptor layout,
and the `flat` presentation are invariants. The selected and raw-center lanes
must use the same source and material implementation.

## Material Boundary

The public `flat` and `filtered-detail` presentations remain unchanged.
`filtered-detail` retains one deterministic `1024 x 1024` RGBA8 texture with
11 mips and the current two planar samples per fragment. No image asset or new
descriptor is introduced.

The generated channels should provide three decorrelated responsibilities:

1. coherent mesoscopic normal relief;
2. broad mineral and weathering identity;
3. broad roughness variation with restrained local breakup.

The fragment interpretation uses existing normalized height, classification
normal, ground, rock, snow, and ambient-visibility inputs. It may strengthen
gradual lowland-to-upland and soil-to-exposed-rock separation, but must remain
mineral-led. Green terrain that implies missing grass, contour bands, visible
tiles, planar streaks, and a uniform high-frequency noise blanket are rejected.

Local normal response must fade as the projected footprint grows and on faces
where planar projection would stretch. The result is still a far backdrop, not
a substitute for close geometry or displacement.

## Lighting Boundary

The accepted shared atmosphere, exposure, aerial-perspective integration, and
directional-shadow implementation remain fixed. Terrain-local refinement may:

- use the classification normal for broad diffuse-irradiance response;
- reserve the filtered normal primarily for direct and raking light;
- rebalance the existing material-aware ambient visibility and roughness;
- retain the continuous snow daylight response across the horizon.

The review rejects ambient flattening, crushed cavities, detached or noisy
shadows, binary twilight changes, and snow that reads as emissive at night.

## Review And Acceptance

Run:

```sh
projects/terrain/capture_visual_closure_review.sh control
projects/terrain/capture_visual_closure_review.sh candidate
projects/terrain/capture_visual_closure_review.sh finalize
```

The matched headless pack under `outputs/terrain/visual-closure-v1` includes:

- selected placement at four daytime headings;
- selected and raw-center placement under day, raking, twilight, and night;
- selected/raw `500 m` views and one selected `100 m` stress view;
- fair-cloud day and twilight composition;
- albedo, material-normal, roughness, ambient, and direct diagnostics;
- steady and moving-clock profiles.

The candidate must improve broad and mesoscopic separation in at least three of
four qualified headings without regressing the fourth. The 200 m and 500 m
views must remain coherent; the 100 m view records the support limit without
expanding it.

At `1600 x 900`, combined terrain atmosphere, shadow, surface, stage, and post
must remain at or below `1.10 ms` mean and p50 and within `0.10 ms` of the
matched control. P95 remains evidence. Finalization also requires identical
frozen metadata and a pixel-identical `flat` control.
