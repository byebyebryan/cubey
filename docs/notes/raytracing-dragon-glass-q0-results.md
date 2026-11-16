# Dragon glass Q0: convergence and native motion measurements

2026-11-08. Q0 measurement work follows the
[quality research plan](raytracing-dragon-glass-quality-plan.md) on local baseline
`10328ef2dff12bd26b6ff20a589c47dde5e8eabc`.

**The full-HD correctness gate failed.** The measurement loop is complete, but
Q0 has not qualified a full-HD high-SPP reference or cleared Q1–Q3 promotion.
No optical normal, footprint, sampling policy, scene, model, recipe or preset
was changed. The implementation adds bounded native telemetry and opt-in
measurement/analysis tools. No commit, push or public-site update.

[Internal comparison, plots and native recording](http://starship:8787/reports/03ec5d44dabdb95928be5797c063694437a035107b795b65bd10b25f801e752b/)
has verified HTTP delivery and a browser check of the image/reference controls,
region tables/charts and 43-second video. It is separate from the public site.

## What the measurements establish

- Sampling noise and faceted optical structure are separate. Increasing SPP
  reduces raw error; it retains the current geometric-face interfaces.
- The small solar source dominates the measured HDR variance. At 128 SPP in
  the canonical dragon-interior proxy, combined-lighting HDR RMSE is 1.680;
  sky-only is .00894 and sun-only 1.680, each against its own pair of independent
  2048-SPP references. This does not imply every pixel has a large error: the
  corresponding combined p99 absolute channel error is .0413. Rare bright
  samples strongly affect RMS. No solar/sample clamp is used.
- Every recorded native frame with received drag motion starts at one sample.
  The higher sample cap affects settling after a stop, not those moving frames.
- Xvfb presentation dominates native host time. GPU trace and host/present CPU
  cost are reported separately. These runs do not establish desktop frame rate
  or physical-display/input latency.
- A rare invalid-medium event appears beyond the earlier 256-SPP acceptance
  coverage. It reproduces independently of the larger path budget or shadows.
  Correctness investigation comes before a visual-policy change.

## Static comparison and reference uncertainty

The frozen baseline executable renders the canonical, elevated and grazing
views at 960 × 540. Each has 32/128/512/2048 SPP with seed 7 and independent
4096-SPP references with seeds 19 and 43. All 18 HDR captures are finite,
nonnegative and have zero invalid-medium, event/query/shadow-limit or stack
flags. Bit 8 remains zero-weight termination with the raw cutoff fixed at zero.
Three separate primary-ID captures supply region labels; those records do not
contain accumulated transport status.

The canonical dragon-interior proxy has these raw HDR results:

| SPP | RMSE to the mean of two 4096-SPP seeds | Estimated candidate noise |
| ---: | ---: | ---: |
| 32 | 4.530 | 4.524 |
| 128 | 1.699 | 1.684 |
| 512 | .9468 | .9187 |
| 2048 | .4951 | .4388 |

The estimated noise floor of that two-reference mean is .2291. Extending both
reference seeds to 8192 SPP reduces its estimate to .1747; the 2048-SPP candidate
then has RMSE .4448 and estimated candidate noise .4091. Both 8192-SPP captures
retain clean transport status. The reference is still noisy and is not ground
truth, a filtering-accuracy pass or a universal convergence claim.

For references A and B with equal independent sample counts, the noise-variance
estimate for their mean is `mean((A-B)^2)/4`. Candidate noise subtracts this
estimate from candidate-to-reference MSE, floored at zero. Two seeds provide a
moment estimate, not a confidence bound, especially for rare bright paths.
Seed-7 sample prefixes are correlated. Their error against independent reference
seeds is useful, but agreement between prefixes alone does not prove accuracy.

Display metrics use fixed-exposure RGB8 PNG code values. Their reference is the
mean of two independently tone-mapped PNGs, not a tone map of mean HDR. They are
display comparisons, with nonlinear/quantization limits; the reference-floor
subtraction is not a radiance-accuracy claim.

Regions include dragon interior, head/thin features, a lower-body/slab crop,
directly visible slab and a silhouette band. Labels come from a single jittered
primary-ID capture; erosion narrows the boundary contribution but does not make
them accumulated coverage masks. The lower-body region mixes reflection and
transmission. Empty sky is excluded from foreground scores. The full-HD label
plan uses a 960 × 540 proxy because the existing query readback is capped at
32 MiB; that cap was retained. The first attempted oversized diagnostic was
rejected and preserved, then replaced with the bounded primary-ID capture.

Sky/sun/combined ablations use two 2048-SPP references per lighting mode at
canonical and elevated views. Default/deep-budget comparisons at 128 SPP in all
three pilot views have whole-image HDR RMSE at most `1.56e-9`. This low-SPP
agreement does not supersede the full-HD failure below.

## Full-HD failure and bounded diagnosis

The full-HD ladder passes at 32, 128 and 512 SPP, then stops at 2048 SPP with
one pixel reporting bit 16, invalid medium. A separate diagnostic reproduces
the 2048-SPP raw buffer byte-for-byte.

| Control | Pixel (908, 344), canonical, 1920 × 1080, seed 7 |
| --- | --- |
| 1335 samples | Clean status |
| 1336 samples | Invalid medium, bit 16 |
| 1336, deeper 256 / 2048 / 8192 budget | Same invalid-medium status |
| 1336, shadows disabled | Same invalid-medium status |

The failing zero-based sample index is 1335. The production budget is
128 / 1024 / 4096, with zero raw throughput cutoff. No event, radiance-query,
total-query, shadow-limit or stack flag accompanies this failure.

This isolates a radiance-path medium failure; it is not a shadow-only or
path-truncation problem. The first invalid event still needs a replay recording
to distinguish an inside miss, a wrong-instance hit and a boundary-orientation
disagreement. No root cause or new mesh defect is asserted. The accepted model's
certificate/hash and all spawning/traversal policies remain unchanged.

The capture tool writes failure receipts before stopping. Its optional
`diagnostic_continue` API retains explicitly labeled failed diagnostic captures;
it does not make them accepted controls or change the production qualifier.
The original failed capture predates this receipt follow-up and is retained
alongside the byte-identical, fully receipted reproduction.

The full-HD independent-reference suite was not completed after the strict
failure. The elevated full-HD ladder is also deferred. Neither is silently
replaced with a lower-resolution or rejected result.

## Native instrumentation and timing scope

With `--metrics`, native frames retain steady-clock update/record timestamps,
filtered drag/scroll/reset input, accepted camera pose, extent, history epoch
and recorded sample count. The journal is bounded at 20,000 frames, with an
explicit dropped count. It allocates no new GPU buffers. CPU record timing ends
before journal packing, submission and present; host profile spans include
those CPU costs. Without metrics, no native journal or timing read is added.

The zero-based host profile frame is explicitly joined to the one-based
FrameClock tick. Analysis checks the join against `host.record_frame`, the
renderer sampling journal and GPU samples; it rejects shifted identities or
divergent sample receipts. Escaping the window can produce a final poll-only
host frame, which is not a rendered frame.

The final recording-free runs use a fixed 200-pixel, two-second drag at a
60 Hz injection cadence, a stop and a return. The accepted camera returns to
the canonical pose. Both extents retain three AS builds, one target generation,
512 settled samples, no camera rejection and no dropped telemetry. Setup,
reset, active accumulation and settled dispatches stay separate. Each frame
that receives nonzero drag motion records one new sample and ends at one sample.

The report and machine-readable analysis retain p50/p95 trace time, CPU record,
host frame intervals and acquire/submit/present CPU spans. Xvfb's present-call
cost dominates the host loop, so sample-cap times on this surface include that
presentation limit. Settling starts at the final camera update and ends at CPU
recording of a sample count; GPU execution and presentation follow.

A separate one-way 1080p orbit/settle recording enables Vulkan synchronization
validation. Recording overhead is excluded from the timing-table runs. Native
dynamic frames have no per-frame transport-status readback; clean Vulkan
validation is not a claim of clean optical medium status for every motion ray.

The earlier instrumentation trial is retained separately: it exposed the
one-based/zero-based join difference and a 12-second hold too short to reach
512 samples through Xvfb. It is not included in final performance comparisons.

## Validation and preservation

All 23 scoped CTests pass: 19 raytracing and four documentation checks. Seven
opt-in statistics/frame-join tests pass; Ruff and clang-format checks are clean.
The baseline and final telemetry build produce byte-identical HDR and PNG files
for both the clean 160 × 120 / 128-SPP control and the full-HD 1336-SPP failure.
Instrumentation therefore retains the numerical failure rather than hiding it.

Exact captured native executable/source and compiled shader copies are retained.
The native source was formatted after capture; the final executable is separately
hashed and checked with those byte comparisons. Both original/certified model
hashes, canonical materials, existing presets and shader sources remain unchanged.
The original SDF worktree was not written. These validation passes do not clear
the failed full-HD optical gate.

## Next loop and acceptance

Recommend a narrow **Q0.1 numerical replay/fix** before Q1:

1. Replay the exact pixel/sample, recording the first failing ray's origin,
   direction, medium, primitive, barycentrics, spawn data and event depth.
2. Compare against a double-precision local geometric query and make the
   smallest justified correction. Retain medium-error reporting, model identity,
   full-scene paths and the existing budget/zero-cutoff policy.
3. Re-run the exact prefix, independent high-SPP full-HD seeds and existing
   optical controls. Full-HD reference acceptance requires zero invalid/budget
   flags. A faster or prettier result cannot substitute for that gate.

After correctness, the retained comparison supports a guarded smooth-normal
experiment for the faceted appearance, paired with consistent footprint work.
The solar-variance and one-SPP motion findings also motivate a separate sampling
budget study. This pass selects no denoiser, normal approximation or new light
transport method. Caustics, GI, clouds and model changes remain outside Q0.

[Compact evidence and reproduction](../evidence/raytracing-dragon-glass-q0-2026-11/README.md)
retain the failed gate as well as passing controls. Full binary buffers, PNGs,
native logs/profiles, intermediate trials and frozen executables remain under
`outputs/raytracing/dragon-glass-q0-20261108/`.
