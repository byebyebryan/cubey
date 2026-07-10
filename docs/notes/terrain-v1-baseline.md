# Terrain V1 Baseline Review

Date: 2026-07-10

This note preserves the original generator revision 1 checkpoint. Revision 2
corrects upper-32-bit seed aliasing and is compared against the broad-noise
control in [`terrain-source-bakeoff-v1.md`](terrain-source-bakeoff-v1.md).

## Implemented Slice

The active `projects/terrain` reboot now reaches the planned first boundary:

- one terrain-owned world-space mountain source;
- an interior-only patch product generated with a 32-sample halo;
- source height, support, derivatives, and local relief;
- open-boundary priority-flood repair, fractional routing, physical
  contributing area, primary-tree Strahler order, normalized discharge, sink,
  and flow-boundary diagnostics;
- queued scalar PNG export plus a versioned manifest and content hash;
- one CPU-product mesh consumer with scalar debug views and oblique, top, and
  near-surface cameras.

There is no independent terrain formula in GLSL. The renderer uploads the
published `height_m`, normals derived from that field, and selected diagnostic
colors.

## Review Pack

Run:

```sh
projects/terrain/capture_review.sh
```

The ignored output under `outputs/terrain/v1-upland-catchment/` contains three
seeds (`0`, `9012`, `12345`), each with an oblique surface render, top height
view, top discharge view, all 15 scalar fields, and a manifest. Seed `9012` also
includes near-surface and flow-direction views.

The deterministic product hashes for this baseline are:

| Seed | Content hash |
| --- | --- |
| `0` | `9316043754484376841` |
| `9012` | `8089601708711949926` |
| `12345` | `10276832735207063406` |

## Findings

The source succeeds as a starting point rather than a final mountain model:

- all three seeds produce coherent broad uplands with meaningful elevation
  contrast and no patch-local authored feature;
- adjacent patches agree on source height, support, slope, and curvature;
- the source still reads smooth and rounded in places, and some ridge contours
  retain the reference formula's regular, cell-like character;
- the simple surface presentation makes those shape limits visible instead of
  hiding them with texture detail.

The regional hydrology is mechanically useful but not yet a river product:

- synthetic tests establish pit repair, downhill routing, converging stream
  order, and fractional area conservation;
- the default seed has no receiverless interior sinks after repair and reaches
  stream order 7;
- large enclosed basins require substantial filling: seed `9012` reaches about
  `367 m` maximum and `34 m` mean fill over the published interior;
- those basins create broad routing facets, direction plateaus, and grid-aware
  drainage paths in the debug views;
- the output is bounded regional evidence. It does not claim cross-patch flow
  continuity, selected rivers, or carved channels.

These artifacts are the next model question. Do not paint a river mask over
them or tune material shading to disguise them.

## Cost Checkpoint

After removing an accidental per-vertex field-summary loop, measurements on the
current workstation at the default `257x257` patch are:

- about `985 ms` for generation, 15 PNG encodes, and the manifest;
- about `1.28 s` for generation, mesh construction, Vulkan startup, and one
  `1280x720` headless render;
- `18 s` for the full three-seed review script, which intentionally regenerates
  the product for each independent renderer invocation.

This is a workbench checkpoint, not a streaming budget. The source and process
remain CPU reference implementations; no LOD, cache, parallel generation, or
GPU generation decision should be inferred from these timings.

## Next Boundary

Before selecting visible rivers or carving terrain, evaluate why the source
produces the large closed basins and whether routing should use depression
breaching, flat-resolution gradients, a coarser drainage surface, or a larger
regional watershed context. Keep source terrain, repaired routing terrain, and
eventual carved terrain as separate named products throughout that work.
