# Native Terrain Diffusion flow-site survey V1 — 2026-09-25

## Question and evidence boundary

Can the existing immutable Terrain Diffusion corpus provide a native-resolution
site for a dry-start, continuously supplied stream, rather than a burst that
ends in a pool?

The hydraulic pilot below established sustained flow, but the user later
rejected its small, low-relief presentation as too flat and subtle. The
[macro hillside follow-up](fluid-25d-hillside-flow-study-v1.md) is the accepted
visual direction: pronounced terrain and continuous uphill supply without a
prescribed outlet. This note retains the earlier evidence and its limits.

This pass follows the [two-site raw-bed preflight](fluid-25d-native-sustained-flow-preflight-v1.md).
That preflight rejected its two proposed stream sites; it did not establish
that the cached corpus lacks rivers. This survey samples more locations in
the same 12 unique payloads without generating new seeds or editing elevation.

Static drainage directions are locators, not simulated water. Priority-flood
heights are used only for analysis and never replace the raw solver terrain.
Depression fill volumes are equilibrium geometry proxies, not required
transient input volumes or recommendations for source discharge.

## Frozen discovery contract

- Use the pinned inventory and exact native 30 m float32 elevation transform.
- Compute shared-face (D4) full-map drainage, contributing area and positive-fill
  components once per payload. Evaluate at most 20 diverse reaches per payload.
- Seek roughly 600–1500 m reaches with positive raw net fall. Moderate grade,
  a coherent source patch, several-cell channel width and modest pooling are
  preferences, not universal physical laws. Small adverse steps are allowed.
- Use the same five-cell cardinal source footprint for every proposal: a centre
  cell and its four face neighbours. Report each cell's raw height and fill depth.
- Measure contiguous raw sublevel widths belonging to the routed corridor at
  0.5, 1 and 2 m stages. A nearby unrelated valley must not inflate these widths.
- Compare full-map and cropped depression geometry. Cutting through a closed
  basin must not turn it into an apparently natural draining reach.
- Retain exact input/crop hashes, source coordinates, route, contextual crop,
  expected boundary exit, gauges and explicit rejection reasons.
- Return a small ranked shortlist for primary review. Ranking alone does not
  promote a site or certify hydraulic behaviour.

## Conditional hydraulic contract

Only a credible site advances to the simulator. Freeze at most two recipes
and common Q/2Q inputs before running at most four hydraulic cases, each with
at most two hours of physical evolution.

The experiment must be a separate opt-in study: initially dry raw terrain,
one continuously active localized mass source with no imposed momentum, no
rain, no prewetted corridor, no interior sink and no terrain modification.
All perimeter faces are outward-only. The expected outlet is an observation
window, not a forced drain. Existing authored demos, neutral TerrainCase
None/RainPulse/SheetRelease protocols and product defaults remain unchanged.

Report front and gauge arrivals, pooling/breakthrough, sustained discharge,
other-edge loss, water/tracer conservation, numerical status and measured
performance. A dye pulse follows established flow in the best healthy case.
Matched top-down and oblique captures must distinguish physical time from
playback acceleration. Keep the strict oracle tolerances unchanged and state
the actual time/state scope of any parity check.

If no credible site is found, close with bounded reproducible survey evidence;
do not compensate by increasing supply, modifying terrain or expanding seeds.

## Discovery result and primary selection

The single corpus pass evaluated **240 reaches across 12 unique payloads**.
**127** passed the basic geometry/crop gates; **15** met the frozen scoring
preferences. Whole-map survey times totalled 27.75 s (6.11 s for the cold first
map, then approximately 1.91–2.08 s per map). These are offline CPU survey
times, not Fluid solver performance measurements.

The primary compared metrics across all gate-passing records, not just the
five automatic shortlist entries, then visually inspected the selected sites.
Several scoring leaders had wide but censored transects:
their apparent width was a lower bound on a broad slope, not evidence of two
channel banks. The scoring formula remains unchanged. Human site selection
prefers a visibly defined corridor over that automatic ranking.

| Frozen pilot | Native crop `(x,z,w,h)` | Source spread | Raw net grade | 1 m geometric widths at five stations | Selection reason |
| --- | --- | --- | --- | --- | --- |
| A: `f072dd7bc0-1592-1852`, desert-low-relief | `(1556,1834,48,48)` | 1.315 m | 1.109% | 240, 90, 60, 150, 210 m | Both bank stops observed at all five stations; modest local pools; 1.8 km analytical route to crop edge. |
| B: `7e0d5cfc47-1983-1478`, dry-upland | `(1947,1448,48,48)` | 0.707 m | 1.148% | 450, 390, 360, 90, 390 m | Broader-slope contrast with narrower sections at lower stage; 1.65 km analytical route to crop edge. |

A remains **borderline** under the frozen score: its 1 m width p10 is 72 m,
below the 90 m preference. It is not fully resolved everywhere (its narrowest
sample is two cells). B is a scoring-preferred reach but most 1 m transects
are censored. Neither is promoted into a product fixture or a surveyed river.

An independent plain heap-based cardinal crop flood matched every recorded
source-to-edge crop-fill sample for both sites. Primary checks also matched
manifest/raw/crop hashes, source heights, cardinal steps and cycle-free routes.
This validates the reviewed cropped analysis; it is not an independent replay
of the entire 12-map drainage calculation or a hydraulic solver certificate.

The tracked opt-in recipes are
[`site-a.json`](../../projects/fluid/fluid_25d/fixtures/native-flow-study-v1/site-a.json)
and [`site-b.json`](../../projects/fluid/fluid_25d/fixtures/native-flow-study-v1/site-b.json).
Their exact transformed crop SHA-256 values are respectively
`f9f44245369b2a7dace07063cf2cab2750b60c0e899b51d0d54e3cf7d1d0a613`
and `5e96f238ffc13d539f8b4f7660177573b94ad8cbd058cc67a50cdaf6f5aee166`.

## Frozen hydraulic matrix

Both sites use **30 and 60 m³/s**, initially dry, for 7200 physical seconds.
The common finite-volume settings are 2 s outer steps, eight 0.25 s solver
substeps, gravity 9.81 m/s² and linear flow damping 0.15/s. Each source uses
the same five-cell footprint; source depth rates are Q/(5 × 900 m²).

Q is a common stream-scale hypothesis, not a bankfull promise. A rough
gravity/linear-damping balance at grade 0.011 gives speed about 0.72 m/s;
a 90 m wide, 0.5 m deep section would then carry about 32 m³/s. That motivates
the rounded 30 m³/s baseline and its 2Q contrast. No depression volume was
treated as required input or used to infer a source rate.

The runner's fixed screening gate requires zero status flags, finite and
nonnegative reported state, water-ledger closure at the existing 0.03% scale,
source accounting, zero sink/non-edge loss, and boundary attribution closure.
For an established expected-route story, all three gauge centres must remain
at least 0.01 m deep throughout the last 600 s; late expected-window outflow
must be at least 0.5Q and total boundary outflow at least 0.8Q. These are
diagnostic selection thresholds, not changes to solver wet/dry behaviour.
Strict CPU/GPU parity remains a separate check with unchanged tolerances.

Evidence is stored under
[`outputs/fluid/native-flow-site-survey-v1-20260925`](../../outputs/fluid/native-flow-site-survey-v1-20260925):
`survey.json` retains the complete gate-passing candidate records and all
rejection reasons; `primary-review.json` pins the reviewed identities, two
recipes and independent checks. The `primary-review-*.png` sheets show raw
terrain, every raw route-cell height and analytical directions—not water.

## Hydraulic result

All four fixed cases completed the two-hour horizon with zero finite-volume
status flags and passed the frozen numerical-profile and sustained-route
screening gates. This is measured simulated flow, not the analytical survey
route painted onto terrain.

| Site / supply | Midstream / downstream first wet (s) | Expected exit first outflow (s) | Late expected-window / total outflow (m³/s) | Final storage (m³) | Maximum water residual (m³) |
| --- | --- | --- | --- | --- | --- |
| A / 30 | 412 / 958 | 1504 | 21.11 / 29.99 | 49,858 | 36.96 |
| A / 60 | 326 / 780 | 1258 | 37.27 / 59.98 | 86,431 | 84.32 |
| B / 30 | 432 / 938 | 1286 | 27.54 / 29.91 | 46,216 | 20.89 |
| B / 60 | 340 / 766 | 1066 | 54.42 / 59.87 | 81,689 | 51.78 |

First-wet uses centre depth ≥0.01 m; first outflow uses ≥1 m³ cumulative
expected-window discharge. Late rates cover physical seconds 6600–7200.
Water tolerances are 64.8 m³ at Q=30 and 129.6 m³ at Q=60, the existing
0.03% cumulative-input scale. All cases have zero sink/non-edge outflow;
boundary-attribution error is at most approximately 0.000001 m³ after CSV
rounding. Source ledgers record exactly Q × 7200 s.

**A / 30 is the presentation pilot**, chosen for its more defined corridor
and smaller inundated footprint, not the highest automatic score or the
largest discharge. Its first measured 600 s established-flow window ends at
2150 s. Late expected-window discharge is about 70.4% of supply, with 8.88 m³/s
leaving other non-corner edge cells. The marked five-cell exit window does
not contain every physical outflow; total discharge nearly matches supply.
B is a useful broader-slope contrast, not a failed hydraulic case.

Aggregate GPU solve means were 0.0847–0.0849 ms per 2 s outer step;
p95 was 0.0861–0.0869 ms, after excluding the first ten samples. Each sample
contains eight solver substeps on **48×48 native 30 m cells**, on the NVIDIA
GeForce RTX 5070 Ti, driver 610.57.04. This excludes render, diagnostic
readback, terrain loading, capture and encoding. The profiled command wall
times were 11.66–11.80 s per two-hour case. These measurements do not certify
large-grid performance or justify interpolating extra terrain detail.

The hydraulic evidence and exact commands are in
[`native-flow-pilot-v1-20260925-validated`](../../outputs/fluid/native-flow-pilot-v1-20260925-validated)
under `hydraulics.json`, the four metric/pass profiles and final PNGs. The
earlier `native-flow-pilot-v1-20260925` folder retains four failed launches
caused by the runner's incorrect crop-option spelling; those commands exited
before any simulation. The runner was corrected to the existing
`--fluid25d-terrain-crop-x/z` options and now fails immediately on a command
error rather than reporting a successful matrix.

## Motion, dye, and how to read the captures

The four labelled videos contain 3600 frames at 1280×720 and 30 fps. Each
video frame advances one 2 s physical step: **60× playback**, two hours shown
in two minutes. The first frame is after 2 s; the initial fields were dry.
The labels show the current physical time, not the playback time. Raw videos
are retained alongside them; their container ends at the final frame PTS,
119.966667 s, while labelled containers include that frame's duration at
120 s. All contain the same 3600 frames; `capture-file-audit.json` records
the probes and SHA-256 hashes.

- [Flow, top-down](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/flow-top-down-labelled.mp4):
  follow the advancing wet edge from the green source toward the amber exit.
  Cyan is shallower water, dark blue is deeper water; colour does not mean speed.
- [Flow, oblique](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/flow-oblique-labelled.mp4):
  provides terrain context. Its moving surface highlight is a render-only
  velocity-advected cue, not conserved material or waves.
- [Dye, top-down](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/dye-top-down-labelled.mp4)
  and [oblique](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/dye-oblique-labelled.mp4):
  magenta/violet tracks the passive conserved scalar carried by accepted water
  flux. It is a concentration reading aid, not extra water or a velocity colour.
- [Flow timeline](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/flow-timeline.png)
  and [dye timeline](../../outputs/fluid/native-flow-pilot-v1-20260925-validated/dye-timeline.png)
  are selected actual video frames, not new simulations or analytical paths.

The dye pulse is frozen at physical seconds **3600–3720**, after the measured
600 s established-flow window. Only dye input stops at 3720 s: water supply
continues throughout. In the top-down movie, dye starts at playback 60 s,
the central reach changes visibly around 68 s, and the lower reach around
76 s. A nearly stationary blue water outline after establishment is expected;
the travelling dye demonstrates ongoing transport. Lingering pink patches
identify slow exchange/storage, not continued dye injection.

At the native 30 m spacing the zigzag shoreline is visible, and the narrowest
survey section is only two cells wide. This pass preserves the data and grid;
it does not claim smooth banks, fully resolved channel hydraulics, branches,
rain-driven channel formation, erosion, or measured natural-river fidelity.

## Tracer and strict-oracle verdict

The 60 s dry-start strict CPU/GPU final-state oracle **passed**, with unchanged
tolerances. The 7200 s winner dye final-state oracle **failed**. This failure
must not be hidden behind the water-profile screening result or the green
repository tests, and the earlier near-dry velocity failure is not its cause.

The long-run log reports status 0, maximum depth error approximately 0.000001 m,
momentum error rounded to 0, velocity error approximately 0.000001 m/s,
tracer-q error rounded to 0, and concentration error 0.000006. Water source and
boundary ledger errors were 0.206 and 6.052 m³, within their existing relative
scales. Tracer amount error was 0.000070 m³. The failing quantity was the
**tracer sink-ledger difference: 0.162678 m³**, versus the 0.003 m³ minimum
tolerance for the CPU's zero sink amount. No tolerance was changed.

The failed final oracle aborts before exporting its requested profile/PNG.
An identical GPU-only winner dye replay retains `winner-dye-profile.metrics.csv`,
its exact command and `winner-dye-analysis.json`; it is not a new hydraulic
setting or a successful substitute for the strict oracle. Its water/solver/
boundary profile metrics match the undyed winner exactly at CSV precision.
It records 3600 m³ tracer input, 3598.841617 m³ boundary export, 0.992838 m³
remaining and -0.002867 m³ final residual; maximum absolute residual over the
profile is 0.005903 m³. Recipe transects first contain ≥1 m³ sampled tracer
at 3602, 3832 and 4172 s; first ≥1 m³ cumulative boundary tracer export is
4532 s. These are diagnostic thresholds, not parcel travel times.

The GPU profile confirms the unexpected 0.162678 m³ sink amount despite the
exactly zero physical sink field. Code inspection localizes the accounting
path to the existing shader's `sourced_h > 0 ? h / sourced_h : 0` retained
fraction and `max(0, sourced_tracer_q - updated_tracer_q)` removal ledger.
With no sink, that fraction should be exactly one; the CPU's double calculation
records zero removal. GPU division/optimization roundoff is a plausible cause,
not a proven instruction-level diagnosis. The amount is about 0.0045% of dye
input, but it still fails the strict contract. This pass does not change shared
solver arithmetic. The immediate numerical follow-up is a zero-sink tracer
accounting control/fix and replay of this pinned case, not more site searching
or higher supply.

Thus the survey succeeded in finding continuous-flow sites and a readable
motion pilot. **The long dye lane remains exploratory, not strictly certified
or promoted into a default/product fixture.** Manual GUI review remains deferred
while the user is remote.

## Reproduction and review state

Run from the Cubey root (new output directories are intentional):

```sh
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/native_flow_site_survey_v1.py \
  --output-dir outputs/fluid/native-flow-site-survey-v1-replay
rtk proxy python projects/fluid/fluid_25d/run_natural_flow_pilot_v1.py \
  --phase hydraulics --output-dir outputs/fluid/native-flow-pilot-v1-replay
# Review hydraulics.json and final PNGs before selecting the winner.
rtk proxy python projects/fluid/fluid_25d/run_natural_flow_pilot_v1.py \
  --phase evidence --winner a:30 --output-dir outputs/fluid/native-flow-pilot-v1-replay
```

The exact original app SHA-256 was
`2f2f7630e12348fc245c96b734c02b92c6cffd7cf94671d7e2666c9d87a33125`.
Its bytes are retained as `frozen-fluid_25d` in the pilot output directory.
Use `--app outputs/fluid/native-flow-pilot-v1-20260925-validated/frozen-fluid_25d`
to select that artifact, while retaining its original build/shader dependencies;
this is not a standalone distributable. `hydraulics.json` and `evidence.json`
record runner hashes at execution, exact commands and exits. Subsequent runner
review added complete-frame validation and an end-of-evidence app-hash check;
subsequent C++ cleanup is changed-lines formatting only.

The 2026-09-26 checkpoint review also made failed numerical/strict-oracle checks
exit nonzero after saving their reports, retained malformed-profile errors,
and rejected evidence replays whose recipe differs from the hydraulic run.
These reporting/provenance fixes do not change physics or waive the recorded
long-duration dye failure. The historical runner hashes above remain historical.

The integrated repository gate passed **133/133 CTest tests** in 96.31 s.
Focused Python controls passed **27/27** (11 survey, 9 prior preflight, 7 runner).
The C++ tests include synthetic native import/provenance, malformed recipe
rejection, exact Q/no-sink/all-edge construction, CLI exclusivity, signed gauge
estimates and conserved boundary attribution. Those generated controls are
not hydraulic validation of the Terrain Diffusion corpus. The app/test targets
compile; `git diff --check` is clean. Prior uncommitted preflight work is
preserved, and no commit or push is part of this goal.

After changed-lines formatting cleanup, both targets were rebuilt and all
**17/17 Fluid CTest cases** passed in 45.01 s. A final-build winner dye replay
matched every original GPU dye-profile metric exactly at CSV precision
(`post-format-replay.json`). The rebuilt app SHA-256 is
`8209e033ca860736212969be1953a99703ea2dea97a2b6b943580bce563da951`;
it is deliberately distinguished from the retained capture binary.
`artifact-review.json` records both hashes, recipes and current shader-build
hashes. None of these post-format checks overturns the strict long-run dye
failure. The full 133-test gate was run once before this formatting-only
cleanup; the affected Fluid lane and exact winner replay were rechecked after it.

Luna owned the bounded survey implementation/controls and the opt-in C++
integration. The primary owned site/recipe acceptance, frozen physics, runner,
captures, independent crop review, integrated verification and conclusions.
