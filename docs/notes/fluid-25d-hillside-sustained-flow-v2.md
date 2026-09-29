# Sustained hillside flow V2

## Scope and evidence boundary

Keep the visually accepted [V1 hillside](fluid-25d-hillside-flow-study-v1.md)
and ask what happens under continuous supply for two physical hours. This is
unchanged, generated Terrain Diffusion terrain, not surveyed elevation. No
new seeds, carving, rain, erosion, source tuning, finer grid, or prescribed
outlet are part of this pass. `virtual-pipes` remains the product default;
this study remains opt-in `finite-volume`.

The extended 256x256 run completes with zero sticky solver flags, but **fails
the unchanged water-ledger tolerance from 4110 s onward**. Its first 1800 s
match all 96,300 shared metric values in the retained V1 profile at six-place
CSV precision. Do not describe the two-hour result as validated hydraulics,
or treat a successful image/video export as numerical acceptance.

Evidence root: `outputs/fluid/hillside-sustained-v2-20260928-iidgrX/`.
Failed numerical reports and child logs remain retained. Diagnostic-only
captures and larger-domain comparisons do not waive the failure gate.

## Frozen case

- Manifest: `cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json`.
- Elevation SHA-256: `a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737`.
- Recipe: `projects/fluid/fluid_25d/fixtures/hillside-flow-study-v1/temperate-upland.json`.
- Crop `[1280,1536,256,256]`, native 30 m; no vertical exaggeration.
- Crop SHA-256: `42cc62f823284c41d768bc05b6195cf291879fb4f6f04337a327ab79806ef5d8`.
- Dry start; five face-connected source cells around full-map `[1365,1621]`.
- Continuous artificial 100 m3/s supply; zero rain, explicit sinks, dye,
  source momentum, or scheduled shutoff.
- Outer dt 2 s, 16 substeps of 0.125 s; gravity 9.81 m/s2, damping 0.15/s.
- Every crop perimeter is outward-only to dry exterior at its own bed.
  It is a finite-domain condition, not a natural drain destination.

The optional same-source 512x512 crop `[1152,1408,512,512]` uses the existing
`temperate-upland-512-benchmark.json`, the same physical source and spacing,
and no forcing adjustment. Its crop SHA-256 is
`02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f`.
The observed edge trigger, not an aesthetic preference, determines whether
to run it.

## How to read the observations

Material water is depth >=0.01 m. Active material water has local speed
>0.02 m/s; slower material-water volume is reported separately. This volume
partition avoids confusing a large number of almost-dry, slow cells with a
large pool. It is not an exact stream/pool segmentation or a steady-state
certificate.

Source connectivity follows shared cell faces through the same material mask,
starting at all five source cells. A diagonal or sub-centimetre link is not
connected under this definition. It says whether the measured wet region
connects to the supply, not which parcel travelled there. Farthest wet
distance is straight-line cell-centre distance, not travelled distance.

Minimum edge distance uses cell centres to the nearest perimeter-cell centre.
Material water within 120 m, or any positive recorded boundary export,
triggers the larger-domain comparison. The no-material-water sentinel is -1.
All these classifications are read-only profile observations; they do not
modify water, route it, or add live GUI readback.

### 256-domain observations

| Physical time | Farthest material water | Lowest wet bed below source | Stored water | Export through crop edges | Active share of material volume |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 min | 0.658 km | 245 m | 59,998 m3 | 0 m3 | 99.65% |
| 30 min | 1.943 km | 555 m | 179,978 m3 | 0 m3 | 99.46% |
| 60 min | 2.561 km | 646 m | 323,830 m3 | 36,076 m3 | 98.49% |
| 120 min, diagnostic only | 3.585 km | 750 m | 520,839 m3 | 198,788 m3 | 97.49% |

The 120-minute state has 2008 material wet cells, of which 2005 connect to
the source under the shared-face definition. Slow material water holds
12,989 m3, rather than dominating the volume. The source patch settles at
1352.506 m3 and remains there at the sampled times: the later growth is
downhill storage/export, not an ever-growing pile at the injection patch.
Maximum depth increases from 0.371 m at 10 minutes to 6.983 m at two hours.
These late observations are useful diagnostics, not a numerical pass.
The older `slow_pooled_wet_fraction` is a wet-cell fraction, not a volume
fraction: its 33.6% final value must not be read as one-third of the water
being pooled. The new slow-material volume share is 2.51% under its stated
depth/speed thresholds.

The [water-accounting chart](../../outputs/fluid/hillside-sustained-v2-20260928-iidgrX/256/water-accounting-review.png)
compares injected, stored, active and slow material-water volumes, then the
absolute residual against its unchanged tolerance. Curves sample once per
physical minute; the failure marker uses the full every-step profile.

The first material edge-band encounter is **2328 s (38.8 min)**; first
positive boundary export is **2368 s**. At two hours the crop has exported
27.61% of injected water. The original window is therefore no longer a
boundary-independent hillside experiment at this horizon.

### Numerical failure

Injected volume is exactly 720,000 m3 at CSV precision; explicit removal is
zero and every profiled sticky status is zero. The final water residual is
-372.251741 m3, exceeding the existing 3e-4 injected-volume scale of 216 m3.
The first failed frame is 2054, state after 4110 physical seconds.

The independent, unchanged strict CPU/GPU checkpoints at **60, 600, and
1800 s all pass**. The ladder completes at its planned 30-minute limit;
it is not a strict 7200 s comparison. The separate two-hour water-ledger
failure is neither hidden by that early parity result nor waived by the
green short regression suite. Checkpoint commands, input identities,
PNG hashes and exit codes are in `256/oracles.json`.

Residual already reaches -22.448851 m3 at 30 minutes and -38.438604 m3 at
the first edge-band encounter, before any boundary export. Thus it cannot
be explained solely by boundary-ledger accumulation. The current profile
does not uniquely distinguish transport arithmetic, stored-field rounding,
and later boundary-ledger rounding. No water arithmetic or tolerance was
changed to make this long case pass.

### Triggered same-source 512 comparison

The larger existing crop was run once because of the measured 256 edge
encounter. App, shader map, raw elevation, manifest, source cells, rate and
physics match; only the crop extent/local source coordinates change. Its
child completes with exact source amount and zero sticky flags, but **also
fails water accounting**, starting at 4046 s (67.43 min).

| Two-hour observation, diagnostic only | Original 256 crop | Wider 512 crop |
| --- | ---: | ---: |
| Farthest material water from source | 3.585 km | 5.219 km |
| Maximum wet-bed drop below source | 750 m | 916 m |
| Stored water | 520,839 m3 | 719,597 m3 |
| Boundary export | 198,788 m3 | 0 m3 |
| Minimum material edge distance | 0 m | 1260 m |
| Active share of material-water volume | 97.49% | 98.19% |
| Source-patch storage | 1352.506 m3 | 1352.506 m3 |
| Signed water residual / unchanged tolerance | -372.252 / 216 m3 | -402.954 / 216 m3 |

The [labelled wider-crop still](../../outputs/fluid/hillside-sustained-v2-20260928-iidgrX/512/wider-crop-diagnostic-labelled.png)
shows the wet paths continuing beyond the old cutoff. No material edge-band
encounter or boundary export occurs anywhere in this wider two-hour profile.
This removes the observed perimeter truncation for this horizon; it is not a
certificate of general domain independence or longer-duration stability.
The wider run's residual with **zero boundary export** confirms that export
accounting cannot be the sole source of the water-conservation drift.

`domain-comparison.json` and the retained read-only `compare_domains.py`
compare 27 domain-independent aggregate metrics. They match exactly through
2364 s at six-place CSV precision. The first difference is 0.000001 m3 in
stored water at 2366 s; the original crop first exports at 2368 s. Edge
distances, domain-area ratios and boundary-bin locations are deliberately
excluded. This is aggregate agreement, **not full-field equality or strict
512 CPU/GPU parity**. Both full-horizon numerical gates remain failed.

Observed profile-child wall times were 93 s for 256 and 423 s for 512.
They include every-step GPU readback, whole-grid diagnostics and output work;
the larger run overlapped the capture job. These are not normal interactive
performance measurements or an isolated solver benchmark.

## Continuous local review

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py
```

The launcher checks the immutable inputs, starts at 8x pacing with the close
source camera, and runs until the window is closed. It contains no finite
frame limit or advance-then-pause request. `--camera overview`,
`--view water-isolation` / `flow-inspection`, and `--playback 1` change only
presentation. `--print-command` prints without opening a window. Space
pauses/resumes and R resets dry start. The panel reports physical time,
supply rate, and continuous/paused/inspection-advance status.

The legacy explicit inspection advance is still available for manual review;
only that opt-in command/button computes a fixed interval then pauses. Normal
playback does not stop at 10 minutes, 30 minutes, or the capture horizon.
Long live playback is exploratory: the two-hour numerical failure also
applies to this unchanged physics. No local compositor was available during
this pass, so headless evidence and launcher argument controls are not a new
human GUI acceptance claim.

### Matched diagnostic captures

Start with the [close top-down water view](../../outputs/fluid/hillside-sustained-v2-20260928-iidgrX/256/close-topdown-water-isolation-labelled.mp4),
then use the [close oblique view](../../outputs/fluid/hillside-sustained-v2-20260928-iidgrX/256/close-oblique-composite-labelled.mp4)
to relate the wet paths to relief. The [overview](../../outputs/fluid/hillside-sustained-v2-20260928-iidgrX/256/overview-composite-labelled.mp4)
shows where those paths meet the finite crop edge.

- The green ring is the fixed, continuous source. No destination is prescribed.
- Expanding blue coverage is the wet front; the narrow fingers follow existing
  terrain depressions. It is wetting, not new channel carving or erosion.
- A broad blue region is not automatically still water: local material-water
  speed and volume are measured separately from its visual shape.
- Moving highlights are render-only. With the tracer gate still closed, they
  cannot demonstrate that a conserved parcel travelled along a given path.
- The time caption is physical time, not video or wall time. Each 30 fps video
  contains 3600 two-second states: 7200 physical seconds in 120 playback seconds.
- The red diagnostic banner marks the failed whole-run gate throughout the
  clip; the water-ledger tolerance first fails at 4110 s, not at its first frame.

Each view has labelled `*-checkpoint-01.png` through `*-checkpoint-04.png`
samples at exactly 600, 1800, 3600 and 7200 s. The primary inspected all 12
samples for readable labels, consistent gross wet extent and source placement.
`capture-review.json` retains actual ffprobe timing and reviewed-video hashes.
The views share frozen arguments and app/input/shader identities; they do not
claim per-frame field-hash equivalence or full human video-playback acceptance.

## Separate tracer gate

`tracer-gate.json` and the retained before/after logs reproduce the old
Site A / 30 m3/s, dt2 / 8-substep, 7200 s strict dye case. This is a separate
48x48 control, not dye on the accepted hillside.

The previous shader unconditionally multiplied sourced tracer by h/h even
when no water was removed. A narrow guard now requires actual positive
water removal before proportional tracer sink arithmetic. Water depth and
momentum expressions, CPU reference and all oracle tolerances remain
unchanged. The zero-sink ledger discrepancy drops from 0.162678 m3 to exactly
zero. Focused zero-sink GPU and real-sink CPU/transport-oracle controls protect
the two behaviors separately.

**The unchanged old long strict oracle still fails**, now on a tracer
conservation residual of -0.006234 m3 against its existing 0.003 m3 minimum.
The GPU-only diagnostic retains 0.992906 m3 and exports 3599.000859 m3 of the
3600 m3 pulse, with zero explicit tracer sink. Drift also exists before
boundary export, so this does not uniquely attribute the remaining problem.
The baseline compiled shaders were not separately frozen before rebuilding;
the before log/app/source-commit identity and new shader identity are retained
without claiming a stronger old build fingerprint.

Hillside dye and Transport Inspection remain disabled. There is no new claim
of long tracer conservation or a fully repaired dye system.

## Replay and failure handling

`run_hillside_sustained_flow_v2.py` records every 2-second state and summarizes
600/1800/3600/7200 s. Its phases are hydraulics, captures, and the independent
60/600/1800 s strict oracle ladder, which stops at its first failed checkpoint.
The 7200 s hydraulic profile is never promoted to strict parity.

App/recipe/manifest/raw elevation identities and the complete exported SPV
file map are pinned before and after each phase. The current binary embeds
the verified `build/dev/projects/fluid/fluid_25d/shaders` path. The runner
assumes the selected app uses its adjacent exported shader directory; the
map includes unused/stale SPVs as build provenance, not proof of loading all
24 modules. Every child command and exit code is retained. Output collisions
are refused. Historical V1 runners, fixtures and outputs remain unchanged.
The initial water-profile/oracle runner is frozen at
`runner-water-gate-original.py` (SHA-256
`fefdc2f95b12b3d40ca9a2578ebc4de961fc520b824f912eacd87640ff6207bf`);
the later explicit diagnostic path does not rewrite those completed reports.

Default capture/domain-parent checks require healthy matching hydraulics.
The explicit `--diagnostic-unhealthy` path may retain labelled failed-study
evidence only when the sole hydraulic failures are water-ledger tolerance,
with the forcing, source amount, zero sinks, zero flags and V1 prefix still
matching. It never turns `phase_checks_passed` true and still exits nonzero.
Unlabelled raw intermediates are clearly marked as diagnostic and are not
the review deliverables.

### Repeat the bounded study

Use a fresh output directory for every replay; these commands intentionally
refuse to overwrite completed or failed evidence. Run the first two phases
with the same `<fresh>/256` directory. Replace `<fresh>` with your new path
before running:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_sustained_flow_v2.py \
  --out <fresh>/256 --phase hydraulics
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_sustained_flow_v2.py \
  --out <fresh>/256 --phase oracles
```

For this retained ledger-only failure, the explicit diagnostic paths are:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_sustained_flow_v2.py \
  --out <fresh>/512 --domain 512 --phase hydraulics \
  --parent-evidence <fresh>/256/hydraulics.json --diagnostic-unhealthy
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_sustained_flow_v2.py \
  --out <fresh>/256 --phase captures --diagnostic-unhealthy
```

Diagnostic artifact phases still return **1** even when all child captures
complete. Read `artifact_completion_passed` separately from
`phase_checks_passed`; the latter remains false. The diagnostic flag cannot
be used to waive the original 256 hydraulic gate or the strict oracle ladder.

The binary used here is SHA-256
`73c3ad9e75322910ce08c0c5e4dd01c02273b8aaecf1a7e9a6dc601698aa0295`;
its exported SPV map is
`b23c067009d2518c8d774ced929f302a7b9b0a85d8b8ef76286c5c60722b35be`.
`runner-diagnostic-reviewed.py` preserves the executed diagnostic runner at
SHA-256 `5f669d82ed8c821487f720f07545a6c36762aa2ec2ee6c12e08ef388a267943b`.
These snapshots distinguish the original failed water-gate runner from the
later labelled diagnostic export path without rewriting historical evidence.

## Implementation validation and repository state

- Full dev build succeeds; full CTest gate passes **135/135**.
- Integrated Python controls pass **121/121**, including 22 sustained-runner
  and three continuous-launcher controls. The pinned scientific dependency
  environment is the one declared by the existing terrain study scripts.
- Python syntax checks and `git diff --check` pass. Changed C++ lines produce
  no `git clang-format --diff` changes.
- Whole-file `clang-format --dry-run --Werror` is **not green** with the
  installed 22.1.8 formatter. The same 404 violations occur in HEAD and the
  working copies of all six changed C++ files: app 82, diagnostics source 32,
  diagnostics header 6, tests 275, UI source 9, UI header 0. The unrelated
  pre-existing whole-file formatting drift was not rewritten in this study.
- No compositor was available: native GUI and human motion review remain
  deferred. Headless capture exports and sampled-frame inspection are the
  evidence available here.
- Study execution used parent checkpoint `7b12519e` plus the then-uncommitted
  changes on local `main`. The frozen `closure.json` describes that historical
  pre-commit state, not the later repository state. The user-requested review
  and commit packages the source, runners, controls and this note; generated
  artifacts remain local under ignored `outputs/`. Nothing is pushed, and no
  remote state was fetched.

The subsequent commit review verified all 67 retained artifact hashes and all
14 implementation/documentation hashes against the execution manifest before
editing this repository-state paragraph. An exact copy of the execution note
is preserved in `outputs/fluid/hillside-commit-review-20260928-H2mh4e/`.
The build is unchanged. Fresh validation passes 121/121 Python controls and
135/135 CTests for the commit. This review does not promote either failed
long-run numerical result or claim new native GUI acceptance.

The primary handled the coupled diagnostics, shader/UI work, GPU experiments,
integration and acceptance review. One Luna Max worker owned the bounded V2
runner and controls, including corrections to diagnostic-only failure and
provenance handling. Passing regression tests do not supersede either long
numerical failure above.
