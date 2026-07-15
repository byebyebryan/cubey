# Terrain Cached Backdrop V1 Review

Date: 2026-07-15

Status: accepted fixed-focus terrain v1 backdrop checkpoint.

## Accepted Product

The production `backdrop` path bakes source v2.1 once around the deterministic
stage focus. The high product contains 3,072 angular intervals, 64 hidden
radial intervals from 300 m to 3.2 km, 768 visible radial intervals from 3.2 km
to 16.384 km, and 48 independently cullable sectors. Logarithmic radial spacing
keeps source footprints proportional to distance.

The bake evaluates 2,558,976 source samples once. It derives geometry normals,
rock/snow classification, and bounded ambient visibility from the cached field.
Sector boundaries duplicate the same global field samples; the product tests
measure zero boundary delta. The source topology remains procedural and
world-space. The polar topology is a fixed-focus consumer representation, not
an authored terrain mask.

The high field retains 4,718,592 source triangles for diagnostics. A far-field
index set submits at most 540,672 triangles before culling. Conservative
azimuth selection plus frustum culling limits that further per frame. Runtime
terrain shaders consume only cached position, normal, and classification data;
they do not evaluate source noise, weathering, terrain shadow marches,
tessellation, or material tiles.

## Review Evidence

Run the maintained pack with:

```sh
projects/terrain/capture_cached_backdrop_review.sh
```

It writes `outputs/terrain/cached-backdrop-v1/` with:

- six relative azimuths for seeds `0`, `9012`, and `12345`;
- the 50-250 m radius and 0-30 degree elevation envelope;
- clay, normal, material-weight, projected-edge, and ownership diagnostics;
- a complete unrestricted-yaw orbit and per-pass GPU profile;
- setup/first-frame wall time and process peak RSS.

The accepted RTX 5070 Ti run at 2560 x 1440 recorded 146 post-warmup terrain
GPU samples. `terrain surface` p95 was `0.876288 ms`, below the `1.0 ms` hard
limit. Atmosphere, post, stage proxy, readback, and encoding are excluded from
that gate and remain separately labeled.

The setup measurement at 640 x 360 was `18,396 ms` wall time and `342,728 KiB`
peak process RSS. That scope includes process and Vulkan startup, deterministic
stage search, high-product generation, GPU upload, and one headless frame. It
is acceptable for this workbench checkpoint but is not a final cache-loading or
asset-persistence design.

## Visual Read

The three seed sweeps preserve continuous terrain through a full orbit without
sector holes or visible seam changes. The cached high-field normals retain more
surface definition than the low product while the reduced far-field index set
avoids the original multi-millisecond triangle load. The result is credible at
the intended backdrop distance; it does not promote source v2.1 to mid-field or
surface quality.

The 30-degree envelope captures deliberately expose the inner ownership cut.
Terrain owns nothing inside 3.2 km. A consuming scene must provide foreground
geometry, water, fog, or an intentionally open mid-air composition there. The
stage sphere validates scale and composition but is not a replacement ground
surface.

## Deferred Work

- persistent disk cache and asynchronous load/upload;
- translated focus, streaming, general terrain LOD, and planet integration;
- a richer cached material product and scene shadow integration;
- close terrain, foliage, water bodies, and regional hydrology;
- source v2.1 shape refinement outside the accepted far-field envelope.

The live `control` and `quality` paths remain explicit experiments. They are no
longer terrain v1 acceptance paths.
