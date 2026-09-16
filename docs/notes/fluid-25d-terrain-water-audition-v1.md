# Fluid 2.5D Terrain-Water Audition V1 — 2026-09-15

## Outcome

This is discovery evidence, not a product-case selection. It validates the
anti-authoring boundary:

~~~
immutable elevation -> identical neutral protocol -> measured solver response -> human review
~~~

No elevation was modified. No channel, source mask, sink, outlet, D8 route, or
priority-flood result informed the solver. D8-style terrain inspection remains
selection metadata only.

The five real terrain crops remain visually and topologically more promising
than the analytic River V0 trough, but the current virtual-pipes contract is
not credible enough on their native 30 m relief to promote any of them. It
stays finite and conserves volume closely, yet carries water at grid-scale,
kilometres-per-second speeds across substantial wet areas. That is a numerical
model finding, not evidence that the terrain or any candidate is bad.

## Immutable inputs

Each case is one 256x128 native crop at 30 m spacing, therefore 7.68x3.84 km.
The two hashes are the source elevation digest and transformed crop digest
reported by the application.

| Case | Source / crop (x,z) | Elevation SHA-256 | Crop SHA-256 |
|---|---|---|---|
| Mountain floodplain | presets/mountain-valley-1 (1536,1664) | 2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48 | 9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6 |
| Rolling catchment | presets/rolling-hills-1 (128,1728) | 00fc8836855c52caba7d9b114b00d8ba426d0b9f716825f7e19c42f2e273c28d | b453f3a366f77bd199ebea0835f00b59d197d1c6dcf00c4f17e78c117b00d927 |
| Branching lowland | presets/rolling-lowland-1 (1216,1664) | fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2 | aa6777e657234f2f787a22c5b68071ddf6aa3a1a09414b1afb92d8521dd6ce63 |
| Flash valley | desert-canyon-study/canyon-candidate-1 (1344,320) | 4c6cda32de46801ca52b5edc37a925a947438de59537e55ec7b08c8883f68b51 | fa51db363786a594a63c8eee103454616d7aacab20799f8e1c3e659e40ff674c |
| Basin hypothesis | desert-canyon-study/canyon-candidate-4 (640,128) | 88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b | 89256d772f208603dd39c8e3c35461033aefeaeb153be10b676e0959eb037795 |

## Frozen matched protocol

Every candidate used native 30 m cells, fixed delta 1/60, two substeps
(1/120 s each), gravity 9.81 m/s², damping 0.15 s⁻¹, and the unchanged
0.0001 m numerical wet threshold.

Both protocols open all outward perimeter faces and leave terrain untouched:

- Rain pulse: 1200 mm/hour (0.000333333 m/s) uniformly over all 32,768 cells
  for exactly 6 seconds, then no source for six more seconds. Its recorded
  source ledger at frame 719 was 58,982.973 m³.
- Sheet release: a uniform 0.05 m initial depth, then twelve seconds of
  unforced drainage. Its initial water volume was 1,474,560 m³.

Captures use a fixed 512x256 camera/view at frames 360 (6 seconds) and 720
(12 seconds): catchment, depth, and flow. Profile diagnostics read back only
frames 0 and 719 through the common profile-output, profile-diagnostics, and
diagnostic-interval path. Slow/pooled means wet and at or below 0.02 m/s; it
does not alter the solver.

The repeatable runner is
[run_terrain_water_audition.sh](../../projects/fluid/fluid_25d/run_terrain_water_audition.sh).
The live invocation was:

~~~
projects/fluid/fluid_25d/run_terrain_water_audition.sh \
  /tmp/fluid25d-terrain-water-audition-20260915
~~~

## Frame-719 comparison

Wet and active are fractions of all cells. Outflow is cumulative perimeter
discharge. Residual is stored - initial - source + sink + outflow in m³.

### Rain pulse

| Case | Wet | Active | Stored m³ | Outflow m³ | Slow/pooled wet | Mean active m/s | Max m/s | Residual m³ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Mountain floodplain | 14.03% | 13.13% | 19,130 | 39,852 | 6.44% | 702 | 4,745 | -0.601 |
| Rolling catchment | 4.90% | 3.14% | 55,484 | 3,499 | 35.93% | 1,175 | 4,079 | -0.597 |
| Branching lowland | 2.09% | 1.89% | 51,448 | 7,534 | 9.36% | 2,029 | 10,691 | -0.617 |
| Flash valley | 1.21% | 0.95% | 54,550 | 4,432 | 21.01% | 1,986 | 4,115 | -0.586 |
| Basin hypothesis | 1.18% | 0.25% | 53,923 | 5,059 | 79.02% | 417 | 3,590 | -0.652 |

### Sheet release

| Case | Wet | Active | Stored m³ | Outflow m³ | Max depth m | Mean active m/s | Max m/s | Residual m³ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Mountain floodplain | 52.94% | 52.76% | 899,386 | 575,174 | 3.61 | 1,487 | 14,707 | 0.010 |
| Rolling catchment | 22.66% | 22.19% | 1,333,919 | 140,641 | 4.65 | 1,486 | 4,867 | 0.017 |
| Branching lowland | 8.84% | 8.79% | 1,282,968 | 191,592 | 5.68 | 1,469 | 5,743 | -0.028 |
| Flash valley | 5.65% | 5.58% | 1,294,462 | 180,098 | 11.87 | 1,750 | 4,213 | 0.106 |
| Basin hypothesis | 3.96% | 3.66% | 1,334,695 | 139,865 | 8.89 | 1,019 | 4,335 | -0.006 |

The raw comparison is
/tmp/fluid25d-terrain-water-audition-20260915/comparison.csv, SHA-256
a3c160dc95a1d99efa02d3a13c308cf412a5ae1c56ec54ef5d47e6a412462ae5.

## Capture and host boundary

The ignored evidence root is /tmp/fluid25d-terrain-water-audition-20260915.
It contains 70 PNGs, 10 profile sets, logs, metadata, and a full capture
inventory. The inventory SHA-256 is
0c57966054b52484598638fba6efb7e8e2001ea69e70de096710a73f42aebea9.

Representative frame-720 hashes:

| Capture | SHA-256 |
|---|---|
| mountain-valley-floodplain-rain-pulse-f720-catchment.png | 0a12ccd04160c550ba56e8675df6ccdccd222e886b31d866cc4a673c938d1e85 |
| mountain-valley-floodplain-rain-pulse-f720-depth.png | 565fab9e2a7c29b9a9e8176af6cac20896e96fbc9ded3cc2060cc11c099c2f3d |
| mountain-valley-floodplain-rain-pulse-f720-flow.png | 1f83e6820132cb2ae38cbd80ff2a286d4bac05e0edc41f0ade31e717c217f365 |
| canyon-four-basin-hypothesis-rain-pulse-f720-depth.png | cb289e6f0da9ce088c4d16931357304e8ef75bceafab8c6890927cb89de4a344 |
| canyon-four-basin-hypothesis-sheet-release-f720-depth.png | 573cbcf4909b536ee23d31d215a7cda321f2568934b701a7a9a8a1b019e129f6 |

Host boundary: Starship; NVIDIA GeForce RTX 5070 Ti; driver 610.57.04; Dev
binary SHA-256 259b8ece1d7b8e54012b37f2226ce677cb2c7f49ae3a34c705bbb692ce1be6b7;
source commit 59cd862a2a321b1a3d1854becf1b5127f4b18d32; source worktree dirty
with P1/P2/P3 changes. This is local GPU evidence, not a clean-release
benchmark or physics-validation result.

## Interpretation and next decision

The runs have small relative volume residuals and no nonfinite or negative
state. The failure is not a single shallow-cell speed singularity: active-flow
means are 417–2,029 m/s, and the mountain floodplain has 13.13% active cells
under rain and 52.76% under a sheet release. With a 30 m cell and a 1/120 s
substep, the positivity limiter permits roughly one cell crossing per substep
(3,600 m/s); derived two-axis speed can exceed that. The flow captures visibly
show this grid-scale transfer.

The observed terrain roles are hypotheses for a later, credible solver, not
promotions:

- Mountain floodplain: strongest future floodplain/default-presentation
  hypothesis; widest rain and sheet coverage.
- Rolling catchment: strongest future rainfall/pooling comparison; 35.93%
  slow/pooled wet fraction under rain.
- Flash valley: retained as a stress case because it concentrates movement
  rapidly, not as a default.
- Basin hypothesis: uncertain. It retains most supplied water and is
  slow/pool-heavy, but could be a generator depression amplified by this scheme.
- Branching lowland: visually distinct, but its 10.7 km/s rain maximum is a
  clear reason not to elevate it from discovery evidence.

Do not select a product terrain, add a water table, tune a candidate, or
reinterpret this as hydrology truth. The next main-thread choice is numerical:
establish a bounded, physically credible replacement/comparison for the
virtual-pipes update on real relief, or explicitly keep Fluid 2.5D as a
stylized nonphysical toy before rendering or showcase promotion.

Follow-up finite-volume evidence is recorded in
[Terrain-Water Audition V2](fluid-25d-terrain-water-audition-v2.md). It
retains a shortlist rather than promoting a terrain product.
