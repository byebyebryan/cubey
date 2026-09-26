# Macro hillside-flow study V1

## Verdict

The previous 48x48 natural-flow pilot passed numerical screening, but the user
rejected its visual story: subtle terrain, slow apparent startup, and too much
emphasis on a source-to-exit route. That is a presentation rejection, not a
new numerical failure. This pass changes the experiment to continuous upland
supply on a broad, unchanged mountain domain, with **no prescribed drain**.

One opt-in 256x256 native-30m case shows downhill spreading, branching wet
fronts and an extending stream-like finger. It is not a narrow bankfull river.
The 7.68 km cell footprint contains 1,504 m of raw relief; no vertical
compression, incision, prewetting, painted route or imposed source momentum
was used. The artificial source remains continuous throughout the run.

At 30 simulated minutes, materially wet water reaches terrain 555 m below the
lowest source cell and extends 1.94 km from the source. All injected volume is
still inside this domain; reaching or exporting through an edge is not an
acceptance requirement. This is a useful source-only dynamics experiment,
not surveyed terrain, a mapped river, erosion, rainfall hydrology or calibrated
steep-slope/waterfall physics. The user's initial visual feedback was
"looks okay to me"; that is separate from numerical or physical certification.

## Bounded terrain selection

`hillside_flow_site_survey_v1.py` examines only the existing pinned
`temperate-mountain-valley` and `alpine-range` maps. It evaluates eight
diversified 256x256 domains per map, reports raw terrain separately from
full-map/crop D4 priority-flood locators, and generates a four-site visual
shortlist. Closed basins and routes not reaching an edge are not universally
rejected. No new seeds, terrain generation or physical water simulation occur
in this survey. Full-map context is the cached 2048x2048 native-30m raster.

The primary selected an already evaluated temperate alternate rather than
the highest-relief alpine sites. Its 3 km analytical route has raw face-grade
P95/max 0.416/0.552, versus substantially steeper shortlisted routes. Even
this is an exploratory hillslope, not a small-bed-slope accuracy certificate.
Its source elevations span 4.879 m; the old flat-source preference is not
carried into the hillside objective. Full-map and crop sampled fill depth are
reported independently; analytical fill never enters the solver terrain.

Survey JSON and raw hillshade/profile PNGs:
`outputs/fluid/hillside-flow-v1-20260925/survey/`.
The final deterministic evidence fingerprint is
`cb82c07bf16d1ee132653aafd9d9670fca4cbcb18dfe64d7c97577dab10ed98e`.
The selected-site PNG starts `alternate-parent-selected-temperate-`.

## Frozen input and numerical contract

- Terrain manifest:
  `cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json`.
- Raw elevation SHA-256:
  `a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737`.
- Crop `[x,z,width,height]`: `[1280,1536,256,256]`, exact native 30 m.
- Transformed float32 crop SHA-256:
  `42cc62f823284c41d768bc05b6195cf291879fb4f6f04337a327ab79806ef5d8`.
- Recipe: `projects/fluid/fluid_25d/fixtures/hillside-flow-study-v1/temperate-upland.json`.
- Source centre crop `[85,85]`, full-map `[1365,1621]`; centre plus four
  cardinal neighbours, equal mass input over 4,500 m2.
- Total source 100 m3/s; dry start; zero sink, rain, source shutoff and dye.
- Existing finite-volume solver, gravity 9.81 m/s2, damping 0.15/s,
  fixed outer step 2 s, 16 substeps of 0.125 s; no solver/shader-math change.
- Every perimeter face permits outward-only flow to dry exterior at its
  own bed height. This finite-domain boundary is not a natural outlet claim.
- New exact schema `cubey.fluid25d.hillside_flow_study.v1` omits
  `expected_outlet` and permits empty gauges. It is accepted only with the
  distinct `hillside-flow-study` mode. Old natural-flow schema/mode unchanged.

Progress observations use depth >=0.01 m and the **lowest** source-bed elevation
as reference. Nested bands count cells and actual water volume 20/50/100/200 m
below that reference. Distance is straight-line cell-centre distance, not
travel distance or a parcel tracker. These metrics do not route or force water.

| Ground below source | First material water |
| --- | ---: |
| 20 m | 30 s |
| 50 m | 82 s |
| 100 m | 198 s |
| 200 m | 466 s |

Final source volume 180,000 m3; stored water 179,977.551 m3; boundary export
and sink removal zero; maximum depth 3.525 m; maximum absolute water residual
22.449 m3, below the existing 3e-4 injected-volume scale (54 m3 here).
Sticky solver flags are zero at every profiled outer step. A strict CPU/GPU
startup oracle passes at 60 physical seconds. The 30-minute run has profile
health evidence, **not a full-horizon strict CPU/GPU parity certificate**.
The older natural-flow long dye-ledger limitation is not fixed or certified
by this water-only experiment.

## Reading and live inspection

Green is a fixed source-location ring; there is no amber destination. Blue is
water; its cyan edges are shallower, darker interiors deeper. Early water
spreads into several downslope arms before the longer finger develops. It is
not a burst landing somewhere: the same input continues at every solver step.

Whole-domain Composite shows the terrain context. Source-context Water
Isolation makes the front easy to see but deliberately quiets terrain shading;
use both, rather than judging landform from that dark reading aid alone.
The closer source camera is render-only. The macro study alone omits densely
packed cosmetic contour stripes and uses uncompressed 1x terrain height.

Normal window playback runs continuously with the source always on; there is
no ten-minute simulation limit. Space pauses/resumes normal playback. The
compute-and-pause controls below are optional inspection shortcuts.

The window panel reports physical simulation time, offers a closer-source
camera, and can **compute the next ten simulated minutes, then pause**. Every
ordinary fixed step is executed in batches of at most 16; no water is
teleported or prefilled, and numerical dt/Q do not change. Cancel or Space
stops an advance; Reset cancels it and restores the dry start. The optional
`--fluid25d-hillside-inspection-advance-seconds 1800` does the same from startup.
It is rejected in headless mode, preserving deterministic capture timing.

Launch continuous playback from dry start in a close view:

```sh
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-scenario hillside-flow-study --fluid25d-solver finite-volume \
  --terrain-heightfield cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --fluid25d-natural-flow-recipe projects/fluid/fluid_25d/fixtures/hillside-flow-study-v1/temperate-upland.json \
  --fluid25d-terrain-crop-x 1280 --fluid25d-terrain-crop-z 1536 \
  --grid-width 256 --grid-height 256 --fluid25d-cell-size-m 30 \
  --fluid25d-natural-flow-source-m3-per-s 100 \
  --fluid25d-fixed-delta-seconds 2 --fluid25d-substeps 16 \
  --fluid25d-flow-damping-per-second 0.15 --fluid25d-render-height-scale 1 \
  --fluid25d-terrain-palette-low-m 770 --fluid25d-terrain-palette-high-m 2274 \
  --fluid25d-hillside-source-context \
  --fluid25d-presentation-time-scale 8
```

Add `--fluid25d-hillside-inspection-advance-seconds 1800` only when requesting
a developed, paused inspection view instead of continuous playback.

The actual window launched successfully and exited cleanly before a GUI
screenshot could be captured. No live window is claimed to remain open.
Headless captures were inspected by the primary. The user's subsequent initial
visual feedback was positive. The user subsequently confirmed that the more
pronounced terrain made the flow meaningful and accepted this direction;
they requested continuous playback rather than inspection pauses. No
unretained GUI screenshot is claimed as evidence.

## Performance and artifacts

After the other GPU checks completed, matched 120-second water-only benchmarks
used the same full-map source coordinates and unchanged overlapping terrain:

| Grid/domain | Mean GPU solve per 2 s step | P95 | End-to-end profile/PNG run |
| --- | ---: | ---: | ---: |
| 256x256 / 7.68 km | 0.335 ms | 0.336 ms | 1.99 s |
| 512x512 / 15.36 km | 0.944 ms | 1.008 ms | 5.46 s |

Both benchmark summaries are identical at CSV precision, with healthy ledgers
and zero flags. The 512 recipe is a **short-horizon context/performance probe**,
not a second accepted 30-minute scene. GPU spans include all 16 solver
substeps, exclude rendering/readback/encoding and CPU oracle, and are measured
on RTX 5070 Ti/driver 610.57.04, Debug host build. Only 50 timed samples remain
after discarding the first ten. Larger domains are affordable here; they do
not make physical water travel faster or provide finer than 30 m detail. The
256 domain remains the main reading scene because its 30-minute front has not
reached an edge and a still wider overview makes water smaller on screen.

Authoritative capture binary SHA-256:
`490b0f4cbc06dfac69a93184b6cb3ffc1ddfd2e5d79d143fbb27e03b780482f8`.
Evidence lives under `outputs/fluid/hillside-flow-v1-20260925/`:

- `final-256/hydraulics.json`, metrics/passes CSVs, final PNG;
- `final-256/{whole-domain,source-context,source-top-down}-labelled.mp4`:
  30 minutes physical evolution in 30 seconds, labelled 60x playback;
- `source-context-{600s,1800s}.png`, `whole-domain-1800s.png`,
  `source-top-down-1800s.png`, extracted from the labelled video;
- `final-256/evidence.json` includes commands, hashes and startup-oracle result;
- `final-256/benchmark.json` and `benchmark-512/benchmark.json`;
- `final-256/capture-runner.py` retains the exact runner used for captures.
- `closure.json` verifies video dimensions/duration/hashes, unchanged replay
  summaries and frozen shader identities; binary/shader snapshots preserve
  evidence identities but are not a portable deployment bundle.

The initial `pilot/` simulation succeeded but its runner failed while making
a relative output-path report; retained outputs are not the authoritative
run. `pilot-validated/` is an intermediate render build. `final-256/` is the
final capture build, with identical hydraulic metrics after render-only and
window-control changes. The runner subsequently received a failure-exit gate
fix only: failed child/numeric/oracle phases and malformed-profile/label errors
now persist reports and exit nonzero. No forcing or simulation change follows
from that fix.

Reproduce using `run_hillside_flow_pilot_v1.py` with `--recipe`, `--manifest`,
`--palette-low 770 --palette-high 2274`, and a fresh `--out` directory.
Run `--phase hydraulics`, then `--phase evidence` in that directory.
`--phase benchmark` is the frozen 120-second cost probe. Evidence requires a
matching app/recipe and successful downhill profile. Existing files are not
overwritten.

## Validation and ownership

Luna implemented the bounded survey and synthetic controls, then reviewed the
integration. The primary selected terrain/forcing, implemented the C++ mode,
progress observations and window controls, captured/reviewed evidence, and
fixed the runner failure gate found by Luna.

- Focused Fluid CTest: 17/17 passed before the final render/control build.
- Integrated final repository CTest: 133/133 passed in 86.08 s.
- Python preflight/survey/runner tests: 39 passed after the failure-gate fixes.
- Strict startup CPU/GPU check: passed at 60 simulated seconds.
- Final/earlier hydraulic profile summaries agree exactly at CSV precision.
- `git diff --check`: clean.

Existing uncommitted work was preserved. No commit, push, default change or
terrain modification was authorized or performed by this goal.

## Checkpoint review — 2026-09-26

The user accepted the macro hillside direction and requested review/commit.
The reviewed tree preserves all historical experiments above, while making
continuous dry-start playback the normal launch example. Compute-and-pause
remains an optional inspection aid, not a simulation requirement.

Review fixes preserve reports and fail the earlier natural-flow runner on
numeric/strict-oracle errors, reject changed recipe replays, and verify the
hillside app/recipe identities again at phase completion. A render-only camera
fix keeps hillside orbit limits consistent across whole-domain/close-source
switches, including accepted custom home distances. Regression checks cover
these fixes; no solver math, source rate, terrain, or oracle tolerance changed.

The reviewed app SHA-256 is
`961df8bbedcef4a037b454b0fdd91d90bfdb99fd2d5f1c3a1854daa6f8fc5a0b`.
A fresh 1800-second hydraulic replay in
`outputs/fluid/hillside-review-20260926-AAe5M8/` matches the retained final
capture build's entire hydraulic summary exactly at CSV precision. Its final
PNG is byte-identical (SHA-256
`a2bd9d5b9157aaaa01ce7079bb239bef3190ad3d7b3e396f460739295dcd76dd`).
A fresh strict CPU/GPU startup check passed at 60 simulated seconds; no
full-horizon strict parity certificate is claimed. Python controls: 43/43
passed. The rebuilt integrated tree passed 133/133 CTest tests in 84.28 s;
changed-line C++ formatting and staged whitespace checks are clean.
Historical captures and hashes remain unchanged, and generated
outputs/cache/build artifacts are excluded from the source commit.
