# Scene Ocean Performance Study

Date: 2026-07-23

Status: completed attribution and ablation study; no production quality defaults changed.

## Goal

Measure whether the current ocean can serve as a close scene background as well
as a dedicated project, identify the renderer-owned cost, and rank optimization
work before terrain/ocean integration.

The working `1600 x 900` budgets are:

- ocean-owned core, wave generation plus surface: at most `1.25 ms` p50;
- ocean adapter, core plus ocean-requested cloud products: at most `1.50 ms` p50.

Shared atmosphere background, visible cloud march/composite, and HDR post are
reported separately. They matter to whole-scene cost, but they are not all owned
by the ocean renderer.

## Method

The accepted run used commit `a6feebcd`, an NVIDIA GeForce RTX 5070 Ti, Windy,
`512` half-precision fields, 180 frames, 30 warmup frames, and 147 steady GPU
samples per lane. Time was paused and captures were headless.

The first attempt split atmosphere background and ocean surface into separate
render-graph passes. That added an HDR attachment store/load and changed the
workload. Nested timestamp scopes also serialized normally overlapping GPU work
and added about `0.15 ms`. Neither result is accepted.

The final method keeps the original combined `ocean scene` pass. A matched
`background` diagnostic run skips only the water draw; estimated surface cost is
the difference between the two scene-pass medians. This is an across-run
estimate, not a claim that the GPU work is strictly additive.

The detail-filter controls initially added about `0.12 ms` to the default
adaptive shader through a uniform branch and generic sampler helper. Three
compile-time fragment variants now preserve runtime UI/CLI selection without
taxing the default. The restored control is within `0.004 ms` of the original
`1.930 ms` scene-pass baseline.

Artifacts are under:

```text
outputs/ocean/scene-ablation-20260723-v2/
```

## Baseline Verdict

| Camera | Total p50 | Background p50 | Surface estimate | Core p50 | Budget |
| --- | ---: | ---: | ---: | ---: | --- |
| close | 1.710 ms | 0.405 ms | 0.973 ms | 1.290 ms | miss by 0.040 ms |
| low | 1.639 ms | 0.406 ms | 0.903 ms | 1.221 ms | pass |
| mid | 2.259 ms | 0.405 ms | 1.523 ms | 1.841 ms | miss by 0.591 ms |

The low camera is a viable clear-ocean scene-background checkpoint. Close is
near the target. Mid is not. The fixed wave path remains about `0.318 ms`; the
camera-dependent surface is the primary optimization target.

## Ablation Readout

| Change | Low core p50 | Mid core p50 | Readout |
| --- | ---: | ---: | --- |
| control | 1.221 ms | 1.841 ms | accepted reference |
| mesh cap 256 | 0.971 ms | 1.689 ms | strong low-view gain; modest mid gain |
| mesh cap 192 | 0.915 ms | 1.709 ms | diminishing or negative mid-view return |
| near-cell target 4 m | 0.973 ms | 1.845 ms | planner helps low; mid remains capped at 512 |
| self-shadow 4 steps | 1.040 ms | 1.528 ms | high-value conservative candidate |
| self-shadow 2 steps | 0.951 ms | 1.370 ms | larger gain, weaker quality margin |
| self-shadow off | 0.862 ms | 1.214 ms | diagnostic only |
| bilinear detail filter | 1.111 ms | 1.562 ms | useful fragment saving; small still-image delta |
| detail anti-repeat 0.5 | 1.225 ms | 1.845 ms | no saving because secondary domains still execute |
| detail anti-repeat off | 1.132 ms | 1.474 ms | meaningful far-field cost, but repetition risk |
| shape anti-repeat off | 1.154 ms | 1.558 ms | meaningful cost, but coherence risk |

The geometry sheet shows little still-image difference down to 256 cells, but
that is not enough to accept a lower cap: motion, silhouette stability, patch
handoff, and close-wave shape still need review. Geometry reductions help the
low camera much more than mid, while filter, shadow, and anti-repeat changes
continue to help mid. The renderer is therefore mixed geometry/fragment bound
at low view and predominantly fragment bound at mid view.

The low-sun shadow sheet keeps most of the visual depth at four steps. Two steps
and disabled shadow are useful bounds, not accepted defaults. Bilinear filtering
is close to adaptive in the maintained stills. Anti-repeat-off stills also look
close, but a static frame cannot validate tiling or temporal repetition, so
global removal is not justified.

## Cloud Products

| Lane | Adapter p50 | Shared cloud p50 | Total p50 |
| --- | ---: | ---: | ---: |
| cached, low | 1.249 ms | 2.166 ms | 3.815 ms |
| planar, low | 1.773 ms | 2.675 ms | 4.338 ms |
| cached, mid | 1.868 ms | 1.611 ms | 3.880 ms |
| planar, mid | 2.326 ms | 2.052 ms | 4.335 ms |

The cached reflection product adds only about `0.018 ms` to the ocean adapter.
Planar reflection is visibly more faithful but adds roughly `0.46-0.53 ms` to
the adapter and also increases shared cloud work. Cached reflection is the
practical scene-background source; planar remains a close hero-water option.
Visible cloud cost requires shared cloud optimization or a cheaper composition
policy and should not be hidden inside an ocean-only target.

## Scaling Caveat

The maintained 1440p captures are visually valid, but their timings are not
accepted. Fixed wave and post costs changed with framebuffer resolution, which
cannot be explained by the ocean workload. A rerun encountered a concurrent
`llama-server` workload at roughly 90% GPU utilization.

The harness now rejects broad wave timing distributions and, for the fixed
512-half study, wave medians that drift materially from the control. Resolution
scaling remains an open measurement rather than evidence used in this verdict.

## Next Batch

The next optimization batch should remain inside the isolated ocean project:

1. Add footprint/distance policy for the static bilinear filter variant and
   validate it in motion at low, close, and mid cameras.
2. Reduce far-field self-shadow work, starting from four steps, while preserving
   the current near-field eight-step path.
3. Branch anti-repeat domains out only when footprint/distance makes repetition
   unresolvable. Strength tuning alone does not reduce work.
4. Evaluate a 256-cell or near-cell-planned low-camera geometry tier with motion,
   wire, LOD, and silhouette captures.
5. Rerun clean 1440p scaling before setting any resolution policy.

Do not optimize FFT first: it is stable, camera-independent, and much smaller
than the surface path at the accepted 512-half setting. Do not begin
terrain/coast integration until the ocean quality tiers and owned-cost policy
are explicit.

Reproduce the full study with:

```sh
projects/ocean/profile_scene_ocean_ablation.sh \
  outputs/ocean/scene-ablation
```

Use `LANE_FILTER` for focused runs and `SUMMARIZE_ONLY=1` to rebuild reports from
retained artifacts.
