# Terrain Resolution And Bandwidth Checkpoint

Date: 2026-07-12

Status: implemented opt-in mountain quality prototype. The existing control
renderer and source v1 remain the default.

## What Landed

The prototype separates three previously conflated limits:

- `control/v1` preserves the pre-prototype source, fixed clipmap triangles, and
  direct procedural material;
- `quality/v1` changes only geometry density and material bandwidth;
- `quality/v2` additionally extends the authoritative mountain detail band
  from four to eight octaves, reaching approximately 6 m wavelengths.

The reusable renderer foundation now supports tessellation pipelines and
mipmapped compute-generated 2D textures. Generated mip chains use a dedicated
mip-zero storage view during compute writes and a full-chain sampled view after
linear blits, which keeps Vulkan layout validation correct.

The terrain quality path uses four-control-point clipmap patches spanning at
most sixteen logical cells per axis. Shared-edge projected lengths select
power-of-two tessellation factors from 1 through 64 against a four-pixel target.
Evaluation samples the same authoritative source with generated vertex spacing
as its footprint. Source v2 retains v1 macro, structure, elevation, and
weathering parameters while broadening only mountain detail.

Ground, scree, rock, and snow use seeded 1024 x 1024 periodic procedural tiles
with a 256 m world period and eleven mips. RG stores tangent relief gradients, B
stores albedo variation, and A stores roughness. Wrapped value-noise bands range
from 64 m to 0.25 m and are sampled through warped world-space triplanar
projection. No imported image is a runtime dependency.

## Fixed Review Pack

Generate the pack with:

```sh
projects/terrain/capture_resolution_bandwidth_review.sh
```

It replaces `outputs/terrain/resolution-bandwidth-prototype/` and writes:

- a fixed-camera `control/v1`, `quality/v1`, `quality/v2` comparison;
- quality/v2 seed controls for `0`, `9012`, and `12345`;
- clay, tessellation-factor, projected-edge, source-band, material, weight, and
  shadow diagnostics;
- native 1920 x 1080 captures and identical midground crops;
- a 90-frame traversal and a 60-frame profiled run;
- v1/v2 source reports, measured acceptance metadata, and external
  TerrainEngine screenshots marked as visual oracles only.

The generated metadata records:

| Measure | Result |
| --- | ---: |
| source v1 SHA-256 | `5687ba3d...34e9edb` |
| source v2 SHA-256 | `c9b1f9b9...21b2b456` |
| quality/control material albedo edge energy | `2.0091x` |
| incremental 960 x 540 wall-frame interval | `7.867 ms` |
| quality process device-local use | about `52 MiB` |

The projected-edge shader follows an exact bound when tessellation is not
capped: `ceil_pow2(projected_edge / target)` makes the generated edge no larger
than the four-pixel target. The canonical backdrop diagnostic uses factors
below the 64 cap, so it remains within that bound.

## Review Read

- Quality/v1 removes the control path's obvious broad silhouette faceting and
  adds stable surface variation without changing mountain mass placement.
- Quality/v2 makes the intended source contribution easy to isolate: major
  peaks stay in place while ridges and foreground slopes gain visible
  intermediate structure.
- Clay and source-band views confirm that material detail is not changing
  geometry. Material albedo and normal views separately expose the generated
  texture contribution.
- Three seeds remain coherent world-space fields. No masks, authored paths,
  quadrants, or drainage grids were introduced.
- Repeated validated stills and the traversal show no visible tessellation
  cracks or Vulkan descriptor/layout errors.
- Generated material regularity was initially visible as a woven pattern.
  Replacing sinusoidal bands with wrapped value noise and moving broad albedo
  variation out of low-frequency tile bands removed the dominant artifact.

## Limits

This closes the specific resolution and material-bandwidth gap; it does not
finish terrain rendering.

- Mountain v2 is still a coherent-noise heightfield, not geomorphological or
  hydraulic terrain. Some fine ridges remain generically noisy rather than
  geologically structured.
- Material classification is broad and the canonical mountain frame is still
  dominated by pale rock and snow. It lacks authored geology, debris fields,
  water, vegetation geometry, and close-range ground composition.
- The generated samplers are trilinear but the current sampler foundation does
  not expose anisotropy. Grazing-angle material quality remains below the
  TerrainEngine visual oracle.
- Quality currently supports only mountain. Upland and plains deliberately stay
  on control until the prototype boundary is accepted.
- Tessellation is a required Vulkan capability only when quality is explicitly
  selected. Control remains usable on devices without it.

The next terrain work should integrate this mountain backdrop into one real
scene before broadening source types. That consumer should decide whether the
quality path becomes the new terrain default and expose which remaining work
belongs to shared rendering foundation versus terrain-specific composition.

## Validation

The checkpoint was validated with targeted terrain/core tests, Vulkan source
parity, repeated validation-enabled control and quality captures, the complete
review pack, and the full repository build/test suite.

The focused terrain filter passed `18/18` tests in `76.62 s`. The final full
suite passed `224/224` tests in `1115.39 s`, including atmosphere, cloud, ocean,
planet, fluid, active terrain, hydrology-lab, reference, and legacy gates.
