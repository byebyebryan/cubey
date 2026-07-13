# Terrain Midground Correction V4

Date: 2026-07-13

Status: implementation contract. Terrain source v1/v2, geometry, tessellation,
and runtime defaults remain frozen while the opt-in layered renderer and
deterministic camera planner are corrected.

## Review Read

The v3 pack separates three useful mountain tests:

- seed `0` is the far-field control. Distance and atmosphere leave a credible
  macro silhouette and must not regress;
- seed `9012` is the useful midground case. The layered path adds readable form
  to a broad face and that gain must survive;
- seed `12345` is the stress case. It exposes candidate-only dark ridge
  outlines, dappled material classification, and camera poses dominated by a
  near side wall.

The seed `12345` clay and source-normal views also expose genuinely busy source
v2 morphology: narrow ridges, pointy local peaks, similarly sized corrugation,
and weak hierarchy. Those shapes appear without layered material color, so they
cannot be solved honestly by material tuning. This batch first removes the
renderer and framing confounders; any remaining clay problem routes to a
separate source-v3 morphology batch.

## Frozen Boundary

This batch does not change:

- source v1/v2 parameters, composition, hashes, weathering, or CPU/GPU parity;
- clipmap topology, tessellation, geometry positions, collision, or target-edge
  defaults;
- terrain presets, hydrology, erosion, water, vegetation geometry, or external
  scene integration;
- the default render path or surface-detail mode;
- capture resolution as a substitute for scene-scale terrain quality.

Camera poses may change because framing selection is an explicit target of the
correction. The existing 150 m camera-clearance and 300 m lower-frustum
clearance guarantees remain mandatory.

## Two-Normal Contract

The renderer needs two distinct terrain normals:

1. `classification_normal` comes from the interpolated geometry-footprint base
   gradient plus final weathering. It owns slope, macro material weights,
   vegetation coverage, and broad ambient visibility. It must be identical for
   tile and layered surface detail at a matched camera pose.
2. `shading_normal` starts from the same gradient and may add the layered
   per-fragment source recovery. It owns clay response, triplanar projection,
   material-normal composition, and final lighting. It may add bounded local
   form without changing macro classification.

The existing `normal` diagnostic continues to show the shading normal. A new
`classification-normal` diagnostic exposes the stable macro input.

Height-assisted blending may refine how already-selected layers overlap, but it
must not rewrite the public macro material weights. Its influence is reduced to
`0.12`, remains full through a `3 m` pixel footprint, and fades to zero by
`8 m`. Tile and layered `material-weights` captures must therefore be
pixel-identical.

## Framing Contract

After a candidate's final pitch is known, the planner tests the center and
upper frame for terrain that blocks the intended target too early. It traces a
5 x 3 ray grid at NDC x `[-0.9, -0.45, 0, 0.45, 0.9]` and y
`[0, 0.35, 0.70]`, sampling every 50 m from 100 m through half the selected
target distance.

A candidate is admissible when no more than 2 of the 15 rays hit terrain in
that interval. The highest existing composition score wins among admissible
candidates. If the 64-candidate shortlist contains no admissible pose, the
planner selects the lowest-occupancy pose and breaks ties by composition score.
The report records the tested distance, hit count, occupancy ratio, and nearest
hit distance so the fallback remains visible rather than implicit.

## Acceptance

The fixed tile/layered pack must prove:

- source v1/v2 report hashes and tile/layered height images are unchanged;
- classification normals and material weights are pixel-identical between tile
  and layered paths for seeds `9012` and `12345`;
- seed `9012` retains the accepted bounded material-normal detail gain;
- the canonical preset/seed/profile camera matrix preserves foreground
  clearance and stays within the 2-of-15 near-frame occupancy limit;
- seed `12345` loses candidate-only classification outlines, dappled macro
  weights, and the dominant near side wall;
- seed `0` keeps its far-field silhouette and atmospheric read.

The review must keep clay, classification-normal, shading-normal,
material-weight, material-albedo, final, timing, and memory evidence separate.
No source-v3 work is accepted in this batch.
