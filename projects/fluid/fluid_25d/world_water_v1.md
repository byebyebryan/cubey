# Prepared-world Terrain Water V1

Current composition (2026-10-13): Fluid renders **only the full-resolution
simulation patch**, with no exterior ground or outer LOD. World/site identity
validation and shared climate/material organization are retained. The shared
Terrain foundation still owns wider backdrop/LOD support. See
[patch-only qualification](patch_only_v1.md). The original integration and its
timings below are historical evidence, not current exterior-rendering behavior.

Status: implemented and headlessly qualified on 2026-10-12 Starship. The
prepared-world collection remains explicit; no default replacement or push.

Connect the existing shared world/site packages to the managed native rainfall
demo and solver-neutral captured-water consumer. Keep immutable elevations,
the native session protocol, existing six-site catalogue and rendering defaults.

The explicit `terrain_water_world_sites.json` collection contains the original
mountain control, interior mountain and interior valley. Its three sites and
five established grid presets fit the existing 32-case control bound. Missing
or changed prepared packages disable admission; no inference or substitute asset
is launched. The original collection remains the default.

Prepared cases retain the shared `cubey.terrain.sampled-source.v1` binding with
an optional `terrain_site` identity: package manifest path/hash and world ID.
Shared Terrain package admission validates producer, height transform, world
coordinates, nested coverage and margins. World crops may not shift at borders.
Raster/climate payload identities retain their existing checks.

Coverage is not hydrology: the default 1024²/16m live rain region is nominally
16.384 km square; the approximately 131 km prepared backdrop is ground-only.
The 262 km atlas is a display/navigation product, not solver input. No coarse
outer water splice, full-world simulation, new solver, source editing or rain-off
capture is part of this pass.

Validation checkpoints: strict package/case admission; shared exterior ground
with context-off parity and seam checks; managed selection/reset lifecycle;
matched dry-start 512 mm/h rainfall at 512²/32m (6 physical hours) and
1024²/16m (12 hours); exact rain-on export; Terrain/Fluid visual review; repeated
headless concurrent performance and a ten-minute live soak. Proposed 1080p
targets are GPU scene p95 <=8ms, CPU service p95 <=4ms, displayed-field age p95
<=1s without backlog, and measured pace >=54x for a 60x request. Paced frame
intervals include intentional waiting and are reported separately.

Generated cases, fields and evidence stay ignored under cache/outputs. Capture
time, live simulation time and decorative motion must remain distinctly labelled.
Native GUI/human acceptance is separate from automated headless validation.

## Reproduction

Use the existing optional native IO Python environment for case preparation and
the study harness; the MIT live launcher/validator use ordinary Python. No
environment is installed automatically. Prepare `all` sites/configurations with
`prepare_live_perf_terrain_v1.py --site-catalogue terrain_water_world_sites.json`
into a fresh study's `cases` directory, then run
`study_world_water_v1.py freeze|pilot|confirm|audit|capture|lifecycle|interrupt|performance|report STUDY`.
Only the performance phase must run without concurrent hydraulic/renderer/test
traffic. It preconditions live trials to twelve physical hours; its soak begins
at six hours to fit the existing twenty-hour worker horizon.

Launch the explicit collection with:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py \
  --study outputs/fluid/world-water-20261012-Ck824g \
  --site-catalogue projects/fluid/fluid_25d/terrain_water_world_sites.json
```

Add `--headless --run-seconds N` for the offscreen path. Current Fluid launches
are patch-only for both collections. The old wider-ground checkbox is removed;
`--fluid25d-native-terrain-context off` remains compatible and `on` rejects before
GPU startup, directing users to Terrain. No exterior mesh is prepared or uploaded.

The initial 2-second interruption correctly stopped before action acknowledgement
and cleaned up, but exposed a collector assumption that profiling files already
exist. The collector now retains cleanup/error evidence even before profile
warmup finishes; such attempts remain incomplete, never timing/switching passes.

## Final qualification

Report: <http://starship:8801/fluid/world-water-20261012-Ck824g/>.

- All 15 prepared world cases admitted. The original collection still admits
  all 30 cases. Managed switching acknowledged 51 scripted actions, installed
  21 replacements plus the initial scene, and passed 22 bit-exact GPU field
  audits. Every new installation starts dry/paused. Owned shutdown is clean.
- All three 512²/32m pilots reached six physical hours; all three 1k/16m
  confirmations reached twelve hours under the same 512 mm/h rain, roughness
  and open boundary. Six field/hash/timeline/supply sanity audits passed; these
  are not flux-conservation or calibrated-hydrology proofs.
- Three rain-on exports preserve exact bed/depth and decoded velocity and load
  in Terrain with matching wider ground. Content-identified opt-in bundles are
  under `cache/terrain/water/v1/world-sites/`; the report's `consumer-assets.json`
  supplies their exact matching source paths. No original capture is rebound.
- 24 focused CPU/GPU regressions passed, none skipped, plus portable study and
  collector helper checks. Approved terrain/water/material references remain
  pixel-exact; cross-frontend attachment rounding remains at most 1/255.
- Thirteen isolated 1080p/60Hz timing slots passed the proposed targets. The
  six developed live repeats achieved 59.93–60.07x, GPU scene p95 5.21–5.56ms,
  CPU service p95 2.69–2.78ms and advancing-field age p95 0.140–0.144s.
- The selected original-mountain ten-minute soak achieved 59.99x, GPU p95
  5.53ms, CPU service p95 2.72ms and field age p95 0.141s. Worker RSS medians
  stayed 611.75MiB; viewer early/late medians rose 6.19MiB, peaking at 496.34MiB.
  The increase is retained without an allocation/leak attribution; arbitrary
  duration leak freedom is not established. Freshness did not accumulate lag.
  Renderer/worker groups exited without escalation or leftovers.

Verdict: the shared prepared world now has a practical live 1k water consumer
and a reusable solver-free Terrain capture consumer. The original mountain is
the fast-runoff control; the interior mountain adds collections; the valley is
pooling-heavy but exposes a conspicuous truncated wet rectangle. These are
useful demo/coverage comparisons, not complete lakes or permanent rivers.

The independently derived original-world numerical bed differs from the legacy
case by at most 0.02002m; this is not bit-exact old hydraulic-state parity.
Source terrain, old case/capture payloads and rendering defaults stay intact.
Native GUI/layout/frame-delivery acceptance, 2k steady-state requalification,
far-context shadows and broader water coverage remain separate follow-ups.

## Post-loop review

The controller wrapper had a timeout-only cleanup gap: `subprocess.run` could
kill its immediate proxy while separately launched GPU children remained alive.
Supervised Python controllers now run directly, receive an interrupt to unwind
their existing cleanup blocks, and retain a bounded owned-group cleanup receipt.
The validator likewise waits for its whole launcher group rather than just the
proxy parent. A real subprocess regression exercises timeout cleanup of an
independently owned child; this is CPU/process evidence, not another solver or
performance qualification. Renderer binaries, shaders and numerical inputs are
unchanged by this review fix.
Performance children now also receive the frozen absolute app path, so starting
the harness from a different working directory cannot select a relative binary.

Review evidence is under
`outputs/fluid/world-water-review-20261012-Yiorms/`: actual managed and live
performance controllers were deliberately timed out headlessly after 12 seconds.
Their three and two independently owned child groups respectively exited without
escalation; interrupted performance remains a failed/incomplete receipt. The first
performance check failed before launching GPU work because of the relative app
path; its log is retained, and the explicit-absolute-path repeat passed cleanup.
All 26 focused CTest groups passed again, and the original 34 input/95 runtime
hashes and 13 accepted timing receipts still verify unchanged. Native GUI review
and wider hydraulic coverage remain deferred.
