# Terrain Midground Correction V4

Date: 2026-07-13

Status: implemented and measured renderer/framing correction. Terrain source
v1/v2, geometry, tessellation, and runtime defaults remain frozen.

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
5 x 3 ray grid at NDC x `[-1, -0.5, 0, 0.5, 1]` and y
`[0, 0.35, 0.70]`, sampling every 50 m from 100 m through three quarters of the
selected target distance.

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

## Implemented Checkpoint

The fragment path now derives a geometry-footprint `classification_normal` and
a separately recoverable `shading_normal`. Classification owns slope, material
weights, vegetation, and broad ambient visibility. Layered source recovery,
triplanar projection, material normals, clay, and final lighting use the
shading normal. The new `classification-normal` view is available through the
CLI, GUI, and PNG smoke suite.

Layer height now produces private compositing weights with `0.12` influence and
a 3-8 m footprint fade. It no longer overwrites macro material weights. The
camera report advanced to `cubey.terrain.backdrop-camera.v4`; its independently
tested framing rays cover the full horizontal frame through 75% of target
distance. The first `[-0.9, 0.9]` and 50%-distance attempt missed a clipped
seed `12345` side wall during visual review, so it was corrected before this
checkpoint rather than accepted from metrics alone.

Generate the accepted pack with:

```sh
projects/terrain/capture_midground_correction_review.sh
```

It replaces `outputs/terrain/midground-correction-v4/` and records:

| Measure | Tile | Layered | Result |
| --- | ---: | ---: | ---: |
| changed height pixels, seeds 9012/12345 | - | - | `0 / 0` |
| changed classification-normal pixels | - | - | `0 / 0` |
| changed material-weight pixels | - | - | `0 / 0` |
| material-normal Laplacian energy | `3.8645e9` | `4.96541e9` | `1.2849x` |
| observed 960 x 540 frame interval | `21.1576 ms` | `22.7045 ms` | `+1.5469 ms` |
| device-local use | `52.00 MiB` | `73.25 MiB` | `+21.25 MiB` |

Source v1/v2 hashes remain unchanged. The complete 18-plan report has no pose
above the 2-of-15 occupancy limit. Seed `0` retains its distant macro read;
seed `9012` keeps the useful layered face response; seed `12345` no longer has
candidate-only macro classification or a clipped near side wall.

## Remaining Boundary

Seed `12345` still exposes busy source-v2 morphology in both clay paths:
similarly sized corrugation, narrow ridges, abrupt local shoulders, and pointy
peaks. Layered shading makes that existing bandwidth more visible, but the
matching classification and weight products show that it is no longer being
invented by macro material selection. A later source-v3 batch should address
mountain hierarchy directly and keep this pack as the renderer/framing control.

The layered candidate remains opt-in and mountain-only. This checkpoint does
not promote it, add close-surface fidelity, retune roughness broadly, or change
the source, mesh, hydrology, vegetation, and external-scene boundaries.

## Validation

The validation-enabled v4 pack passed source-hash, image-identity, camera,
detail-band, frame-budget, and device-memory gates. The full dev build passed,
and the repository suite passed `228/228` tests in `1123.19 s`.
