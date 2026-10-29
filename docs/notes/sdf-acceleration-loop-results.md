# SDF empty-space acceleration: first-loop results

Date: 2026-10-28. Scope: E0–E3 of the
[research plan](sdf-acceleration-research-plan.md). Status: completed bounded
comparison; sampled/full remains the default. Nothing pushed or adopted.

[Interactive HTML report — Empty-space acceleration results](http://starship:8787/reports/97230212e85f8861e2663f916c7d17c8dcccf618a9684d35d2b10e1856bb7720/)
contains the scene/view/resolution controls, native images and evidence links.

## Decision

Follow-up: the [current-candidate shader investigation](sdf-guardian-frame-profile-results.md)
identifies a substantial compiled-shader cost and a guarded, byte-identical
approximately 21 ms diagnostic. The measurements below remain the original
comparison; its 51 ms route was the general shader, not a lower bound on this
scene's source-tracing cost.

Keep the separate far-region bound route as an opt-in research candidate. It
reduces Guardian's regional payload from **56.78 MB to 6.44 MB**, without a sampled
field, and substantially improves completed source-history transitions. It does
not solve Guardian's frame cost or meet its 100 ms edit target. Root bounds have
view-dependent value. The unclamped hybrid's modest gain does not justify its
additional 913 MB field in this comparison.

The next proposed experiment is to forecast a finer level over active exact
cells and profile the remaining interpreter/surface-query cost. Do not proceed
automatically. A GPU builder remains conditional on a useful query structure;
this loop implements neither finer regions nor GPU construction.

## Measured frames

All timings below were collected on the shared RTX 5070 Ti, driver 610.57.04.
An existing NVENC encode was active earlier in the batch; the retained boundary
snapshots around the cited full-HD rear runs report 0% encoder activity. No
encoding overhead is established for those runs, and no unrelated process was changed.
Three interleaved repetitions use 180 frames, 30 warmup and 150 completed GPU
samples. The table gives ranges of per-run whole-scene GPU p95, with ordinary
native shaders and matched scene/lighting/view settings. Do not substitute the
earlier diagnostic's 10.13 ms value for this control.

| Route | Guardian preset, 320x180 | Guardian rear, 1920x1080 | Robot preset, 1920x1080 |
| --- | --- | --- | --- |
| Sampled default | 0.459–0.464 ms | 2.107–2.121 ms | 4.092–4.112 ms |
| A64 ordinary corrected control | 13.644–13.646 ms | 62.831–63.026 ms | 6.694–6.865 ms |
| H64 clamped hybrid | 18.002–18.016 ms | Not measured | 9.997–10.098 ms |
| H64 unclamped hybrid | 17.316–17.334 ms | 72.169–72.499 ms | 9.946–9.979 ms |
| A64 root bounds | 13.563–13.567 ms | 63.064–63.306 ms | 6.289–6.381 ms |
| A64 far bounds | 13.272–13.328 ms | 51.289–51.384 ms | 6.272–6.334 ms |
| A64 root plus far | 13.259–13.263 ms | 51.164–51.459 ms | 6.319–6.338 ms |

The full [timing receipt](../evidence/sdf-acceleration-loop-2026-10/timing.json)
includes the preset, side (+90 degrees) and rear (+180 degrees) views at all
eligible sizes. Guardian's preset source routes fail the 320p <=12 ms guard;
their 960p and full-HD results remain **Not measured**. Eligible views pass the
960p <=50 ms progression guard, but Guardian's measured full-HD source routes
remain above the 16.7 ms target. No extrapolated full-HD value is reported.

There are 318 accepted timing runs, 47,700 completed GPU samples and 20 skipped
resolution groups. Per-run GPU/encoder utilization, clocks, temperature and
power are retained. These results are not idle-GPU acceptance measurements.

## Why the work reduction is larger than the frame reduction

In the Guardian preset replay, root plus far bounds reduce total primary,
normal and shadow CSG visits from **18.75 million to 9.65 million** (48.5%).
Far bounds alone save essentially the same work. The reduction grows in side
and rear views, but the visible-hit programs remain about **78 instructions on
average, p95 195, maximum 307**. Robot's corresponding mean is about 4.9.

Thus empty-space work and storage were real costs, but removing them leaves an
expensive near-surface tail. This does not independently identify GPU register,
cache or divergence costs; hardware profiling is still required before making
those claims. CSG visits are not GPU ISA instruction counts.

Guardian has 249,086 far cells of 262,144 at axis 64. Its remaining exact programs
have the same resolution and source semantics. No grid-resolution change hides
the effect. [Workload receipts](../evidence/sdf-acceleration-loop-2026-10/workload.json)
keep timings separate from counters and record primary/normal/shadow work,
far advances, exact far-cell fallbacks, hit/miss work and interval rejection.

## Completed history transitions and memory

The existing content-history benchmark uses ten forward/backward prefix
transitions, repeated three times. Two transitions per run reach the complete
source, giving six complete-source samples. Expected-state compilation occurs
before the request timer. Completion includes requested compilation, caches,
upload, publication and the first rendered frame followed by queue idle at
320x180. It is not physical input/display latency or continuous-brush acceptance.

| Scene/route | Ten-action p95 range | Complete-source transition p95 | Complete-source min–max |
| --- | --- | --- | --- |
| Guardian A64 | 498.9–531.6 ms | 521.6 ms | 482.9–521.9 ms |
| Guardian far | 177.4–197.9 ms | 170.9 ms | 156.7–172.2 ms |
| Guardian root plus far | 178.8–181.7 ms | 165.8 ms | 151.1–166.0 ms |
| Robot A64 | 92.4–95.5 ms | 95.5 ms | 90.3–96.6 ms |
| Robot far | 22.7–28.4 ms | 26.3 ms | 21.1–27.6 ms |
| Robot root plus far | 23.7–26.6 ms | 25.8 ms | 21.5–26.1 ms |

The [action receipt](../evidence/sdf-acceleration-loop-2026-10/actions.json)
retains every accepted command and transition. The first Guardian control and
its initial replay overlapped owned CPU checks; a final serial replay replaces
that group. Earlier receipts remain in the raw directory. No other accepted
GPU timing job ran alongside another owned GPU job. Background encode load
remains a shared-machine qualification limit.

The far route's cold CPU region build/upload is also much cheaper; the per-run
resource metrics retain that component separately. Do not equate cold build time
with edit completion. Robot's far payload is 4.42 MB versus the 11.35 MB exact-only
control. The sampled and hybrid field metric remains 912,997,552 requested bytes.

Portable staging uploads place the selected source/regions on observed device
memory type 1, heap 0, flags 1. Old and pending region payloads overlap during
publication. [Memory bookkeeping](../evidence/sdf-acceleration-loop-2026-10/memory-envelope.json)
includes that overlap, staging/CPU serialization allowances, builder scratch
and source payloads. Its conservative requested-geometry envelope is about
41.2 MB for Guardian far and 1.27 GB for Guardian hybrid, below 2 GiB. This is
not allocator-resident or whole-process peak memory measurement.

## Qualified contracts and retained limitations

- Six source variants pass **930 frozen independent checks**, **254,220 derived
  GPU point probes**, and **120 semantic publication/undo actions**.
- The three-view camera matrix passes **2,073,600 dense queries** and **37,800
  sparse comparisons**, including full-source and instrumented/ordinary controls.
  No exhausted rays or dense/sparse geometry failures were observed.
- Twelve native glass captures retain active transport and zero exhaustion.
  Eighteen opaque captures also have zero exhaustion pixels.
- Expanded CPU checks cover 3,539,082 bound points, including 3,153,180 far-bound
  inequalities, independent known surfaces, transformed primitives, cell corners,
  complements, workspace clipping, exact far-cell queries and capacity fallback.
- All 54 historical shader binaries and 138 protected scene/preset JSON files
  retain their hashes. The source/material/history formats and sampled default
  remain unchanged.

The native images are **not pixel-identical**. Guardian changes at most 132 of
57,600 pixels with a largest channel delta of 2/255. Robot's preset includes one
high-contrast material/CSG boundary pixel at (145,62), with a 184/255 channel
change and a different native diagnostic normal/owner. Other Robot views have
maximum channel changes of 1–8/255. The full source evaluator functions are
unchanged; changed hit positions can select a different side of a boundary.
The generated camera-query ray at that pixel passes its fixed depth tolerance
but does not reproduce the native normal change. The
[focused receipt](../evidence/sdf-acceleration-loop-2026-10/material-boundary.json)
keeps those evidence scopes distinct. This visual boundary limitation is retained
for later refinement; no production visual acceptance or default switch is claimed.

The numerical claims are the current qualified engineering envelope, not a
universal FP32 proof. The unclamped hybrid keeps interpolation/R16/source guards
and an independent lookup allowance. Bounds never replace exact hit/normal or
material values. Root sublevels propagate through smooth unions; subtraction
retains the body's bound. Far certificates use the final clipped field, explicit
exact/far/fallback kinds and unchanged inherited parent certificates. Cache and
source allocation complete before publication; appearance actions reuse geometry.

## Evidence and reproduction

See the [evidence index](../evidence/sdf-acceleration-loop-2026-10/README.md) and
[execution record](sdf-acceleration-loop-progress.md). Implementation starts at
`61152fb` and `f1971e0`; later test/tool checkpoints are pinned in the evidence.
The evidence context records complete source/shader/binary identities.

Build Release and run the private study tools in fresh directories, one owned
measurement job at a time. The private `--acceleration-variant` values are
`off`, `control`, `unclamped`, `root`, `far`, and `root-far`. Selected variants
require static authored headless source tracing, pruned evaluation and axis 64;
the mode/variant pair is validated before allocation. The exact-only candidates
allocate no `Field` or sampled atlas. No live editor, general warps, finer grid,
GPU builder, asynchronous publication, push or default adoption is part of this
completed loop.
