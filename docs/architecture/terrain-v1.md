# Terrain V1 Patch Product

Date: 2026-07-10

Status: implemented source bakeoff. Current findings are recorded in
[`../notes/terrain-source-bakeoff-v1.md`](../notes/terrain-source-bakeoff-v1.md).

## Goal

`projects/terrain` starts as a deterministic local terrain patch product, not a
biome gallery and not a renderer-owned height formula. The first slice proves a
world-space upland source, named derived fields, bounded regional hydrology,
scalar exports, and a mesh consumer over the same CPU product.

This slice stops before visible rivers, channel carving, water, material
products, vegetation, streaming, LOD, or planet integration. Those systems need
stable field truth before they can become consumers.

## Patch Request

The project-local request contains:

- a `cubey::procedural::PatchDomain2D` describing the requested interior grid,
  world origin, cell size, world seed, semantic space, and patch address;
- recipe id plus its required generator revision;
- default `upland-catchment-v1` revision `2`;
- comparison `upland-broad-noise-control-v1` revision `1`.

The default interior is `257x257` samples at `32 m` spacing, approximately
`8.2 km` per side. Generation uses a fixed 32-sample process halo. Interior
dimensions must be odd and at least 17 samples; cell size must be finite and
positive. The patch address identifies a patch but does not perturb source
sampling. World position and world seed are the source of terrain truth.

## Product Contract

`TerrainPatchProduct` returns the validated request, an interior-only
`FieldSet2D`, field summaries, and a deterministic content hash. Source and
process calculations run over the bordered sample grid and are cropped before
publication.

The first product fields are:

| Group | Fields |
| --- | --- |
| Source and geometry | `source_height_m`, `mountain_support`, `height_m`, `slope`, `curvature`, `local_relief_m` |
| Regional routing | `routing_surface_m`, `routing_fill_delta_m`, `flow_direction_x`, `flow_direction_z` |
| Drainage diagnostics | `contributing_area_m2`, `stream_order`, `discharge_proxy`, `sink_mask`, `flow_boundary_mask` |

The broad-noise control also publishes `uplift_potential`, `macro_mass`, and
`base_relief_m`. `height_m` equals `source_height_m` for both recipes, and
hydrology cannot modify it. The corrected contour source and shared-foundation
OpenSimplex control have no runtime dependency on `terrain_ref` and no
independent GLSL implementation.

## Hydrology Boundary

The regional process uses open-boundary priority-flood epsilon filling followed
by D-infinity-style fractional routing to at most two lower receivers. Every
cell contributes one cell area of uniform runoff. Accumulation is reported as
physical `contributing_area_m2`; Strahler order uses the primary-receiver tree;
`discharge_proxy` is a normalized log view of contributing area.

Hydrology is bounded regional evidence, not independently tile-seam-safe truth.
The halo reduces interior boundary damage, and `flow_boundary_mask` marks core
flow that continues into the halo. Source height and local derivatives must
seam across adjacent patch requests. Whole-watershed planning, cross-patch flow
state, channel selection, and incision are later contracts.

## Consumers And Review

`terrain_generate` exports every product field through `CaptureQueue` and
writes a v2 manifest with request identity, halo/boundary policy, field
distributions, fixed display metadata, morphology review metrics, content hash,
and filenames. The `terrain` app builds a finite review mesh from
the CPU product and exposes surface, source/height, derivative, routing, area,
order, discharge, sink, and boundary views. Presentation color is a consumer
only; it does not add material fields to the product.

Acceptance requires deterministic and full-64-bit seed-sensitive products,
adjacent-patch source seams, halo-stable core fields, synthetic routing tests,
flow mass conservation, fixed-range scalar export validation,
headless/windowed renderer smoke tests, a three-seed recipe comparison, and a
regional source/process frame.
