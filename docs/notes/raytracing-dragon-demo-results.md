# Dragon demo: fixed-quality RT and a matched forward approximation

Follow-up: the [forward correctness audit](raytracing-forward-dragon-audit.md)
corrects the new adapter's tangent attributes and changes the comparison preset
from screen to environment transmission. The initial report and measurements
below retain their original source identities and screen-baseline scope.

2026-11-16. This implements the bounded prototype from the
[transport/comparison plan](raytracing-dragon-demo-transport-plan.md).
Both new routes are opt-in. Existing sampled defaults, cap 100, certified mesh,
slab geometry, material recipes and shared foundation renderer are preserved.

The [browser comparison](http://starship:8787/reports/a9552bb10b43a53f3fb280516ebd1791f964c895352447c9d19c858e1846a619/)
contains wide/close views, a bare-slab control, two/four-sample comparison and
native 1080p camera recordings. Inputs and checks are indexed in the
[evidence record](../evidence/raytracing-dragon-demo-2026-11/README.md).

## Result and scope

The RT glass has substantially different internal structure from the forward
approximation and reaches actual scene geometry along its secondary rays.
Sky reflections alone are a less useful contrast. The existing dragon and
marble slab provide the test without extra props or a floor.

The new mode removes stochastic transport choices and progressive waiting.
At a fixed pose, different seeds and progressive targets produce identical
HDR/PNG output. Every native frame has the same two or four primary samples.
Fine shimmer remains in intricate glass detail during motion; this is not a
promise of antialiasing or optical perfection. The report lets the user judge
the actual movement before any default promotion.

The RT implementation reuses qualified interfaces/spawning, Fresnel, Snell,
TIR and Beer absorption. It explicitly follows both glass branches, prunes
below 1% maximum-RGB throughput and retains existing work/storage caps. Opaque
hits stop with cached PBR. Escaped rays use the visual sky without the sampled
mode's additional narrow analytic sun. A four-sample 2×2 grid is the initial
preset; one two-sample diagonal fallback was tested. There is no GI, denoiser,
new optical-normal model, temporal reconstruction or runtime SDK.

The forward adapter passes the same scene's vertices/indices and recipe inputs
to `ForwardPbrRenderer3D`. It never constructs an RT pipeline or acceleration
structure; its device has RT features disabled. It uses the ordinary estimated
solid-volume exit, projected opaque HDR transmission and environment fallback.
A 0.20-object-unit thickness estimate is held across all views. This is one
practical baseline, not the limit of raster rendering. The native adapter is
intentionally restricted to sharp glass, smooth normals, final view and
default combined clear lighting.

Both comparison presets disable local shadows. Camera, materials, atmosphere,
sun, exposure and display transform match. Forward has one raster sample while
RT has two/four fixed spatial samples, so edge aliasing is not an equal-AA test.
The bare-slab control caught a diffuse-SH normalization mismatch; the new demo
now consumes cached SH like forward PBR. The sampled shader's historical
normalization stays unchanged. The bare-slab full-frame mean absolute RGB-code
difference is 0.041/255; a chosen top-interior region measures 0.305/255.

## Recorded cost and acceptance limits

RTX 5070 Ti, 1920×1080, whole-frame GPU timestamps during camera input in native
recorded/validated Xvfb runs:

| Route | Samples/frame | Moving samples | Median GPU | P95 GPU |
| --- | --- | --- | --- | --- |
| Forward | 1 raster | 215 | 4.41 ms | 4.47 ms |
| RT demo | 2 fixed | 185 | 14.19 ms | 15.99 ms |
| RT demo | 4 fixed | 150 | 27.68 ms | 34.38 ms |

The four-sample route exceeds the provisional 33 ms target at some angles
(35.47 ms recorded maximum). The tested two-sample route provides headroom at
the cost of more spatial aliasing. No extra fallback or reconstruction work
was added. Native recording/presentation cadence is separately about 15/19/22
fps for RT4/RT2/forward on Xvfb. The 30 fps MP4s repeat displayed frames; they
have no interpolation or per-frame convergence. These are not desktop FPS or
physical-display acceptance claims.

Full-HD wide/close output is finite and nonnegative with no invalid-medium,
event, visibility or query-budget failures. The close view has **one pixel at
(1219,760) reaching the existing 16-pending-branch limit**, status bit 64;
its queued contribution is discarded. The wide view has none. This is retained
and reported as a bounded prototype limit, not reclassified as zero-error
optical transport. Explicit 1% policy pruning is separately recorded as bit
128 and the demo optics dump's `14.z` counter.

Twenty existing RT CTests and sixteen asset-bound optical controls pass,
including the original shallow diagnostic truncation cases. New real-GPU
controls verify seed/capture-target independence, finite transport, per-frame
completion, separate pruning diagnostics and a genuine non-RT baseline. The
saved sampled 320×180 / 32-sample / seed-19 / raw-cap-zero reference is PNG
pixel-identical, with maximum raw-HDR float difference 8.94e-8 and identical
transport flags. Bitwise HDR identity is not claimed.

The first pass is ready for visual review. Four samples remain the opt-in
visual preset and two the tested faster option. Existing ordinary/sample
defaults remain unchanged. No commit, push or public-site publication is part
of this pass.
