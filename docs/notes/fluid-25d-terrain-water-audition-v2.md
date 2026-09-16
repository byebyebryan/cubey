# Fluid 2.5D Terrain-Water Audition V2 — 2026-09-15

## Decision

Retain a ranked shortlist; do not promote a terrain into a product fixture yet.

The opt-in finite-volume comparison removes the V1 virtual-pipes
kilometres-per-second failure on the same untouched 30 m Terrain Diffusion
crops. It is stable enough to keep the mountain floodplain and rolling-hills
catchment as the first two product-study candidates, with flash canyon as a
stress candidate. It is not presentation-ready selection evidence: the
oblique catchment captures make near-complete thin-film wet coverage look
raster-speckled, even where depth diagnostics reveal coherent drainage. Both
protocols are still neutral discovery probes rather than a product water story.

This remains the anti-authoring boundary:

~~~
immutable elevation -> one shared neutral protocol -> measured solver response -> human review
~~~

No crop was reshaped. No channel, outlet, source mask, sink, D8 route, or
terrain analysis result participated in the solver. There is no water table,
climate claim, erosion, or sediment state here.

## Inputs and retained identity

Every case is a 256x128 native crop at 30 m spacing (7.68x3.84 km). Production
Terrain Diffusion assets remain outside Git; this table is a reproducible
external-asset recipe, not an asset import.

| Case | Source / crop (x,z) | Elevation SHA-256 | Crop SHA-256 |
|---|---|---|---|
| Mountain floodplain | presets/mountain-valley-1 (1536,1664) | 2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48 | 9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6 |
| Rolling catchment | presets/rolling-hills-1 (128,1728) | 00fc8836855c52caba7d9b114b00d8ba426d0b9f716825f7e19c42f2e273c28d | b453f3a366f77bd199ebea0835f00b59d197d1c6dcf00c4f17e78c117b00d927 |
| Branching lowland | presets/rolling-lowland-1 (1216,1664) | fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2 | aa6777e657234f2f787a22c5b68071ddf6aa3a1a09414b1afb92d8521dd6ce63 |
| Flash valley | desert-canyon-study/canyon-candidate-1 (1344,320) | 4c6cda32de46801ca52b5edc37a925a947438de59537e55ec7b08c8883f68b51 | fa51db363786a594a63c8eee103454616d7aacab20799f8e1c3e659e40ff674c |
| Basin hypothesis | desert-canyon-study/canyon-candidate-4 (640,128) | 88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b | 89256d772f208603dd39c8e3c35461033aefeaeb153be10b676e0959eb037795 |

## Direct V1-horizon comparison

The virtual-pipes V1 evidence remains unchanged in
[the V1 note](fluid-25d-terrain-water-audition-v1.md). The corrected
finite-volume rerun used its exact mountain-rain command: 1/60 s frames, two
substeps, 1,200 mm/hour uniform rain for 6 s, and a 12 s total horizon. Its
frame-719 profile was status 0, stored 58,955.637042 m3, source 58,982.972656
m3, boundary outflow 26.900290 m3, residual -0.435324 m3, maximum depth
0.002164 m, maximum speed 0.002170 m/s, and zero active-flow cells.

The corrected artifact is
`/tmp/fluid25d-terrain-water-audition-fv-v1-matched-postfix-20260915`; its
metrics SHA-256 is
`1b4407cfc3c4d8b71ad5273e3cc3352bcc1917988c5bc8ba6c070da060331264`.
It confirms that 12 seconds is physically too short to evaluate a 30 m-cell
terrain response, rather than merely making the output look subdued.

The pre-correction failed attempt is deliberately retained only as a resolved
regression artifact at
`/tmp/fluid25d-terrain-water-audition-fv-matched-20260915`. Its sole log
records the former negative-ledger diagnostic failure (SHA-256
`d2c62615837a24b79c98d83a620888b6d8ef54aa98a49864e48036afd10e945c`);
it is not part of the positive result below.

## V2 protocol

`run_terrain_water_audition_v2.sh` is finite-volume-only. Its fixed settings
apply to every crop and both protocols:

- frame delta 2 s, eight substeps (0.25 s/substep); the solver checks its
  global CFL condition at every substep;
- all outward faces open to a dry exterior at the same edge-bed elevation;
- rain: 12 mm/hour for 600 s (exactly 2 mm uniformly supplied), then 600 s
  of no source;
- sheet: 0.05 m uniform initial water depth, unforced for 1,200 s;
- 300-frame / 600 s early and 600-frame / 1,200 s late captures, each in
  catchment, depth, and flow views; one final metrics profile per case/protocol.

The repeatable full command is:

~~~sh
projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh \
  /tmp/fluid25d-terrain-water-audition-fv-v2-20260915
~~~

For a bounded pilot only, the same runner supports the frozen-list selector:

~~~sh
CASE_FILTER=mountain-valley-floodplain,rolling-hills-catchment \
  projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh \
  /tmp/fluid25d-terrain-water-audition-fv-v2-pilot-20260915
~~~

`CASE_FILTER` chooses cases only; it cannot alter their forcing, crop, terrain,
or boundary conditions.

## Full-matrix result

Local GPU evidence ran on Starship (GeForce RTX 5070 Ti, driver 610.57.04),
with binary SHA-256
`477db3d9e3efb58a3e672c3976ee3eb0ada36b341b8b017a1fd7bdf2eb8ad809`.
The V2 runner SHA-256 is
`46a7a6b840ed2047c66b6e57557e46a58116f6a08c1fc892e44f97b000ba099f`.
The source commit was `59cd862a2a321b1a3d1854becf1b5127f4b18d32` with the
P1-P4/solver worktree dirty. This is local GPU evidence, not a clean-release
benchmark or field-physics validation.
All ten final profiles report finite-volume status 0. Residual is stored -
initial - source + sink + boundary outflow; it is small relative to each
protocol volume and is reported rather than hidden.

### Rain at 1,200 s

| Case | Stored m3 | Boundary outflow m3 | Max depth m | Max speed m/s | Active | Residual m3 |
|---|---:|---:|---:|---:|---:|---:|
| Mountain floodplain | 57,408 | 1,574 | 0.050 | 0.015 | 0.00% | 1.150 |
| Rolling catchment | 57,695 | 1,287 | 0.045 | 0.013 | 0.00% | 1.351 |
| Branching lowland | 57,614 | 1,368 | 0.050 | 0.018 | 0.00% | 1.195 |
| Flash valley | 57,399 | 1,583 | 0.052 | 0.016 | 0.00% | 1.162 |
| Basin hypothesis | 57,646 | 1,335 | 0.058 | 0.018 | 0.00% | 1.226 |

The rainfall probe leaves nearly all cells wet but below the 0.02 m/s reporting
threshold. Its subtle depth patterns are useful neutral wetting evidence, not
a visual effect or a water-table claim.

### Sheet release at 1,200 s

| Case | Stored m3 | Boundary outflow m3 | Active | Max speed m/s | Slow/pooled wet | Residual m3 |
|---|---:|---:|---:|---:|---:|---:|
| Mountain floodplain | 1,279,281 | 195,275 | 63.04% | 0.618 | 36.26% | -3.469 |
| Rolling catchment | 1,339,364 | 135,196 | 29.50% | 0.827 | 69.80% | 0.213 |
| Branching lowland | 1,245,500 | 229,060 | 24.91% | 1.410 | 74.92% | 0.283 |
| Flash valley | 1,255,318 | 219,242 | 20.95% | 1.451 | 78.94% | 0.141 |
| Basin hypothesis | 1,347,227 | 127,333 | 16.69% | 0.968 | 83.16% | 0.080 |

The status-clean speeds (0.62–1.45 m/s maximum under the sheet) replace the
V1 multi-kilometres-per-second transport. Depth captures visibly separate a
broad mountain pool, rolling drainage, lowland branching, flash-valley routing,
and the basin-heavy candidate. They still show a one-cell-scale numerical
representation, so they do not prove field hydrology.

The ignored full evidence root is
`/tmp/fluid25d-terrain-water-audition-fv-v2-20260915`: 70 PNGs, 10 profile
sets, 70 logs, metadata, and a full capture inventory. `comparison.csv` SHA-256
is `02b644f38550c3696f94bbdb7efbac8bc51a04b5c0363a072b2ad851ed2ea988`;
the `captures.sha256` inventory SHA-256 is
`60df9adc439c12c0fe4055a813cb8f2dc00a175b2b78d4dd891ba9ee3d6e81f0`.

Representative late-capture hashes:

| Capture | SHA-256 |
|---|---|
| mountain sheet / catchment | 84603c76c90c85fd9113ca4b233ded761dc39694e394fb114ff687af9f13535f |
| mountain sheet / depth | e4a40ec2b9b0f817a354b8f078965033da40b8c21e31e73ee86378dc0c66f4bf |
| rolling sheet / depth | 92e1d3b7fb13911b4e411d486eee6d904b1de954b4e84aeb1814d825157d4890 |
| flash-valley sheet / depth | e2e3f6f8142c26620a8cc5f14664d7ab5d28135085b057488f4d4a4fe5fc0eaf |

## Shortlist and reopen gate

1. **Mountain floodplain** — strongest next presentation-study candidate:
   broad retained water and the largest active sheet response. Its exact
   source/crop identity above is sufficient to reproduce it outside Git.
2. **Rolling catchment** — strongest paired rainfall/pooling candidate:
   visible branching with 69.80% slow/pooled wet coverage after sheet drainage.
3. **Flash valley** — retain as a stress case, not a default: quickest sheet
   movement and substantial drainage.

Do not use basin retention as a lake promotion: it may be a source-generator
depression. Do not elevate branching lowland merely because its depth view is
distinct. Neither is a terrain rejection; both are intentionally retained as
evidence cases.

Reopen promotion only when a concrete product story selects a forcing and
boundary contract, the current oblique catchment presentation can communicate
the selected response without its thin-film raster/speckle artifact, and the
selected external raster is available at the use site and matches the retained
recipe. A solver change must rerun the exact V1-horizon comparison and this V2
five-by-two matrix. A water table requires a separate state/product decision
rather than an interpretation of rain or surface pooling.
