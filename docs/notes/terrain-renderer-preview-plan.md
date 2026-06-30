# Terrain Renderer Preview Plan

Date: 2026-06-30

## Context

The mountain stress recipe now has a clearer source hierarchy, but flat scalar
PNGs still make it hard to tell what is a peak, basin, shoulder, or slope. The
next review surface should use Cubey's normal renderer path so height can be
judged with perspective, depth, and lighting.

This is not a request for a CPU software perspective export. `projects/terrain`
already emits the terrain product and debug PNGs; the new preview should be a
consumer of that product, not another terrain source or a parallel renderer.

## Direction

Add a separate `terrain_preview` app target under `projects/terrain`.

- Keep the existing `terrain` executable as the CPU product/debug exporter.
- Generate a local `TerrainRegionProduct` from the selected terrain recipe.
- Adapt the height field into an indexed mesh with normals and review colors.
- Render that mesh through the existing Vulkan windowed/headless app host.
- Provide repeatable camera presets for oblique, profile, and top-down review.

The first review target is the `temperate-mountain-range-stress` recipe because
the active visual problem is mountain height readability. The app should still
accept any current terrain recipe.

## Boundaries

In scope:

- renderer-backed perspective PNG capture;
- small shared run-config additions for terrain recipe, camera preset, and
  vertical scale;
- deterministic mesh construction from `TerrainRegionProduct`;
- headless PNG smoke coverage.

Out of scope:

- terrain algorithm retuning;
- clipmaps, LOD streaming, tiled world paging, and planet integration;
- GPU-displaced height textures;
- foliage, water surfaces, erosion simulation, or atmospheric environment
  integration.

## Acceptance

- `terrain_preview --headless` writes a nonblank PNG through
  `cubey::host::HeadlessPngHost`.
- Oblique and profile captures make the mountain range's peaks and basins
  easier to inspect than `mountain-relief.png` alone.
- The existing scalar terrain exporter and tests keep their current behavior.

## Outcome

Implemented as `cubey_project_terrain_preview` / `terrain_preview`.

- The app uses shared run-config controls: `--grid-size`, `--terrain-recipe`,
  `--terrain-camera-preset`, and `--terrain-vertical-scale`.
- The first mesh adapter uses 32-bit indexed CPU mesh data uploaded to the
  renderer, preserving the distinction between a renderer-backed preview and a
  CPU rasterizer.
- `outputs/terrain/mountain-range-stress/mountain-perspective.png` and
  `outputs/terrain/mountain-range-stress/mountain-profile.png` are the current
  review captures for 3D mountain readability.
- Focused validation passed with terrain product tests, core run-config tests,
  and the `terrain_preview_headless_writes_png` smoke test.
