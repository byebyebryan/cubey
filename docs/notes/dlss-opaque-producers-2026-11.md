# DLSS Quality for opaque SDF and fixed opaque RT

Opaque analytic SDF and the fixed opaque RT dragon now accept
`--upscaler dlss-quality`. Both select preset K. Each also accepts
`--upscaler spatial-quality`, which renders at two thirds of each output
dimension and uses linear HDR sampling in the display pass. The default
`native` path follows each project's ordinary anti-aliasing policy.
[SDF usage](../../projects/sdf/README.md#quality-upscaling) ·
[Fixed opaque RT usage](../../projects/raytracing/docs/fixed-opaque-taa.md#reduced-resolution-controls)

The Viewer, SDF and RT use one optional NGX backend. A build with the
developer-supplied v310.9.16 SDK can pass scene-linear HDR, Vulkan forward-Z
depth, previous-minus-current motion in render pixels, and jitter and reset
state into NGX before display post. SDF computes camera and sky motion from
its trace depth. RT takes depth and motion from the first jittered primary
lane and uses depth 1 for sky. The render target and display target have
separate extents. GPU acceptance advances the view history, and target
recreation restarts it. [Shared input contract](../../include/cubey/render/temporal_upscaler.h)

## GPU cost

RTX 5070 Ti, driver 620.70.06, native Vulkan Release. Each cell reports the
median of three per-run GPU p95 values in milliseconds. Render-only tests
submitted 120 views and retained 96 per run after warm-up. RT total frame
spans include environment updates. The captures kept the same scene, camera
and lighting per producer. RT kept four primary rays per render pixel.

| Scene | Display | Native TAA | Spatial Quality | DLSS Quality K | NGX evaluation |
| --- | --- | ---: | ---: | ---: | ---: |
| SDF robot | 2560×1440 | 17.17 | 9.98 | 10.66 | 0.85 |
| SDF robot | 3840×2160 | 42.06 | 17.05 | 18.78 | 1.88 |
| Fixed opaque RT dragon | 2560×1440 | 19.59 | 9.06 | 9.84 | 0.86 |
| Fixed opaque RT dragon | 3840×2160 | 43.09 | 19.19 | 21.33 | 1.67 |

NGX confirmed K at 1707×960 → 2560×1440 and 2560×1440 → 3840×2160. The
spatial control used the same render and output dimensions. Lower resolution
accounts for the GPU savings. DLSS spends 0.68–2.14 ms more than spatial
scaling on these complete-frame medians and reconstructs more detail.

## Image quality and lifecycle

Each mode rendered 96 matched frames with a hold, orbit, stop and explicit
camera cut. K restored edges around the robot's antenna and face and reduced
speckle on the RT dragon relative to spatial scaling. In the 1440p encoded
crops, mean luma RMSE against a 2× spatial reference was 4.28 for SDF K,
5.32 for native TAA and 6.51 for spatial. RT was 13.13 for K, 13.31 for
native TAA and 15.94 for spatial. K and native TAA had similar changes in
RT luma residual across adjacent frames. The RT reference was rendered at
5120×2880 and area-downsampled. The SDF reference used the native SS2 path.

Vulkan synchronization validation passed at both display sizes. Virtual X11
resizes selected K again at the new extent and restarted history. A committed
SDF cut reset history at the edit and produced the same PNG as an independent
DLSS seed of the edited scene. Native SDF Auto, TAA and SS2 stills and the
forward hybrid, fixed glass and sampled RT stills matched their baselines.
The Viewer replay selected K, M and L as requested through the shared backend.
K passed validation and matched its baseline still. The original M/L NGX
validation signature recurred.

Keep these as opt-in modes for the tested opaque scenes. The timings show a
benefit where trace cost dominates. The Viewer benchmark remains the cheaper
forward comparison. The application-wide selection policy and the
experimental glass path are described in the
[AA architecture guide](../architecture/temporal-aa.md#upscaler-selection).
[Viewer qualification](dlss-super-resolution-2026-11.md) ·
[Temporal producer studies](temporal-consumers-2026-11.md)

Native logs, timing repeats, runtime identities, motion comparisons, resize
receipts and the display-matched captures are retained under
`outputs/dlss/wider-20261118/`. The RT timing set was rerun after concurrent
GPU work and its memory reservation cleared.
