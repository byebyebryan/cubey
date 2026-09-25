# Fluid 2.5D terrain-site water study V1 — 2026-09-23

## Decision

Retain the generated-terrain shortlist as discovery evidence; promote no crop,
fixture, forcing tier, or default. The 512×256 native crops expose more complex
terrain-conditioned drainage than the earlier 256×128 study, especially in the
WaterDepth view under the globally fixed visibility-stress rain. They do not
yet make moving water or its cause and outlet readable in the wide oblique
Composite view. A larger terrain alone does not solve the presentation problem.

The study preserves the [V2 anti-authoring boundary](fluid-25d-terrain-water-audition-v2.md):
immutable elevation → shared protocol per tier → measured solver response →
human review. The morphology scan only proposes windows. Its masks, junctions,
and scores never enter Fluid; no channel, river network, source, or outlet was
painted into the terrain or solver.

## Reproducible inputs

The [scan tool](../../projects/fluid/fluid_25d/terrain_site_scan_v1.py) examines
the existing cached Terrain Diffusion 30 m payloads at pinned code revision
`82a0431281f21a6ec3d691a12ee61525de5b0790` and model revision
`9ef8030cb805b433b98ec25c5dddefbac07a9e26`. It checks source hashes,
deduplicates 16 manifests to 12 unique elevation payloads, and excludes crop
overlaps with the five prior V2 sites by elevation identity. Its 120 m
area-averaged morphology proxy screened 4,409 native 512×256 windows
(15.36×7.68 km); 3,383 had a connected deep-valley component. This is a
topographic-shape heuristic, not D8 routing or mapped hydrology. The [scan
record](../../outputs/fluid/terrain-site-scan-v1-20260923/scan.json),
[candidate table](../../outputs/fluid/terrain-site-scan-v1-20260923/candidates.csv),
and [contact sheet](../../outputs/fluid/terrain-site-scan-v1-20260923/shortlist-contact-sheet.png)
are review artifacts. The earlier scan output was retained under the
`-superseded` sibling after a junction-counting defect was corrected; use only
the linked corrected record.

The [frozen recipe](../../projects/fluid/fluid_25d/terrain_site_water_v1_20260923.json)
pins full manifests and elevation hashes, crop coordinates, a matching
512×256 solver grid at 30 m, and the common capture/acceptance rules. Human
review kept the top three morphology candidates, with rank 1 intentionally a
basin-like contrast and ranks 2–3 visually clearer trunk/tributary candidates:

| Rank | Cached variant | Crop origin (x,z) | Role |
| --- | --- | --- | --- |
| 1 | `canyon-candidate-4` | (256,576) | Basin-like morphology contrast |
| 2 | `temperate-mountain-valley` | (512,832) | Trunk/tributary candidate |
| 3 | `canyon-candidate-2` | (640,896) | Trunk/tributary candidate |

Each site uses the existing terrain-case finite-volume RainPulse solver: dry
initial water, open outflow-only perimeter, 2 s frames with eight substeps,
600 s of uniform rain followed by 600 s unforced. The *same* 12 mm/hour
neutral tier applies to all three sites; the *same* 60 mm/hour
visibility-stress tier applies to all three sites. These supply 2 mm and
10 mm of rain per cell respectively. The stress tier is a legibility probe,
not a climate or field-hydrology claim. There is no explicit inlet or drain:
rain is the source, and the open perimeter is the only outlet.

Run the [study runner](../../projects/fluid/fluid_25d/run_terrain_site_water_v1.py)
from the repository root with the pinned local assets and built app present:

~~~sh
cache/terrain/tooling/v1/terrain-diffusion-venv/bin/python \
  projects/fluid/fluid_25d/run_terrain_site_water_v1.py \
  --output-dir /tmp/fluid25d-terrain-site-water-v1-replay
~~~

The runner fails closed on recipe/source mismatches, uses existing solver and
renderer code unchanged, and writes capture and video inventories. It does
not overwrite an existing output directory or regenerate Terrain Diffusion
output. Its acceptance and exact executable,
asset, and artifact hashes are in the ignored [full-matrix evidence
root](../../outputs/fluid/terrain-site-water-v1-20260923/full-matrix).
This is local GPU evidence from Starship (RTX 5070 Ti, driver 610.57.04)
against a dirty worktree and a provenance-hashed app binary, not a clean
release benchmark or validation against a mapped river.
The completed matrix records the exact runner that executed it (SHA-256
`7af826824e3e058932c98e072b1ec545b420bf033bd1a4b6e35d36ecc4ac45d3`).
The current runner source has a later metadata-only provenance fix (SHA-256
`dd93e0a3b30609f74bbaf1261af2b4b4ae2a4faba220bcabd67339816b059029`);
the GPU captures were not rerun after that fix.

## Measured and visual response

The [full-matrix report](../../outputs/fluid/terrain-site-water-v1-20260923/full-matrix/report.md)
passes all six site×tier cases. Each has 40 regular profile samples, 41
independently rendered frames per Composite and WaterDepth sequence, selected
diagnostic stills, and a 10.25 s H.264 split-screen video. All sampled solver
status flags are zero. The worst conservation residual is 0.0030% of supplied
volume, below the fixed 0.01% gate. The last regular profile is equivalent to
capture `f586`; separately checked `f600` captures complete the 1,200 s
horizon. The following values are **f586 samples**, not f600 measurements:

| Site rank | Tier | Max depth (m) | Active cells | Max speed (m/s) | Boundary outflow (m³) |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | neutral | 0.065 | 0 | 0.016 | 2,775 |
| 2 | neutral | 0.057 | 0 | 0.015 | 2,445 |
| 3 | neutral | 0.057 | 0 | 0.017 | 2,556 |
| 1 | stress | 1.086 | 15,229 | 0.143 | 27,379 |
| 2 | stress | 1.084 | 14,398 | 0.143 | 20,988 |
| 3 | stress | 0.983 | 16,282 | 0.143 | 23,589 |

“Active” means above the existing 0.02 m/s diagnostic threshold, not a
separate solver state. The stress tier has coherent, branching WaterDepth
filaments on all three sites and substantially more measured perimeter
outflow. Neutral shows subtle depth structure but no active-flow cells at the
last regular profile. The rank-2 and rank-3 terrain previews read more like
trunk-and-tributary landscapes than rank 1, but the scan score alone is not a
river-site ranking. The stress [rank-2](../../outputs/fluid/terrain-site-water-v1-20260923/full-matrix/visibility-stress/rank-02-temperate-mountain-valley/review/overview.png)
and [rank-3](../../outputs/fluid/terrain-site-water-v1-20260923/full-matrix/visibility-stress/rank-03-canyon-trunk-tributary-candidate/review/overview.png)
overviews make the gap clear: the lower diagnostic panes show convergence,
while the wide upper oblique panes show only faint water traces. The
[rank-2 split-screen video](../../outputs/fluid/terrain-site-water-v1-20260923/full-matrix/visibility-stress/rank-02-temperate-mountain-valley/videos/composite-water-depth.mp4)
contains the 41 sampled checkpoints; its depth-map half remains the clearer
way to interpret the change.

## Next gate

Before selecting a generated terrain as a product scene, choose the intended
story: a wide rainfall catchment with an explicit rain and drainage cue, or a
closer river reach with visible transported dye/floaters and a designed
source/outlet. For the rainfall story, test presentation changes on the same
frozen site and solver protocol—camera scale, water contrast, and a restrained
source/outflow annotation—then compare Composite readability against these
untouched baselines. Do not relabel the morphology proxy as a real river map,
increase forcing per site, or promote a fixture from this study alone.

## Validation

The scan and runner have 14 passing focused Python tests covering source pins,
overlap exclusion, connected-valley ranking, a matching native solver grid,
fixed forcing, and exact capture acceptance. Runner preflight validates the
three pinned local assets without creating output. The existing focused
`fluid_25d_tests` CTest passes. The completed matrix passes all status,
conservation, capture, and video gates; all 1,212 inventoried files match
their recorded size and SHA-256. None of these checks validates field
hydrology or makes the oblique presentation legible.
