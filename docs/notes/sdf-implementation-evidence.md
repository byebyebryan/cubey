# SDF implementation evidence

Date: 2026-10-21. Work in progress under the
[implementation plan](sdf-projectr-port-plan.md); the
[project guide](../../projects/sdf/README.md) owns run commands and behavior.

## Native field/tracing checkpoint

Device: NVIDIA GeForce RTX 5070 Ti. Clang 23, Vulkan 1.4, `dev` build.
Native headless runs used `--require-validation`; no Vulkan validation errors
were emitted. These are source-derived scenes, not old-Unity image parity.

| Recipe | Bricks | Allocated field bytes | Build wall time, isolated example |
| --- | ---: | ---: | ---: |
| Box | 101,856 | 912,866,464 | 7.0 ms |
| Rotated box | 127,614 | 912,866,464 | 15.9 ms |
| Box subtraction | 116,970 | 912,866,464 | 14.8 ms |
| 512 sticks, seed 42 | 170,600 | 912,866,464 | 18.4 ms |
| 2,048 sticks, seed 42, atlas 92 | 630,656 | 1,974,880,928 | 126.7 ms |

Wall times include filtering, sampling/copying and synchronous construction
submissions/count reads. Some fixture captures ran concurrently. These examples
are diagnostics, not a benchmark distribution or steady frame-time result.

The allocation is smaller than the planning estimate because this port does
not allocate unused donor normal/color fields. It retains paired distance
atlases and dense node headers; edit headers use an explicit two-uint layout
instead of the donor's packed eight-bit count. Batches remain bounded at 128.
Allocation bytes exclude shared host/frame targets and driver overhead.

The 64 and 80 atlas profiles explicitly overflow on the main 2,048-stick
recipe. The 92 profile fits. The source-sized 6,400-stick request overflows at
798,200 requested frontier entries versus capacity 778,688, returns failure
and writes no successful PNG. No profile automatically grows.

Native normal/shaded images of the box, rotated box and cut were inspected.
The raster depth fixture draws a slanted blue plane through the box: the front
section occludes the box, and the box hides the rear section. Captures from
opposing views were inspected. The 512- and 2,048-stick hard-shadow images were
also inspected. Presentation is still plain and has not received user visual
acceptance.

An early boundary-tie defect produced magenta exhaustion near nearly parallel
rays. Directional cell selection at representable boundaries fixed those
images. The fixture oracle covers exact axis rays, brick planes, three external
views and rays beginning inside a solid. Inside rays seek an exit.

CPU/GPU fixture results: rotated box 257 rays, cut 258 rays, zero mismatches,
zero exhaustion and zero sampling ambiguities. Maximum hit-distance difference
was below 0.000056 workspace units. The 512-stick check used 256 rays, zero
out-of-bound mismatches/exhaustion and five sampling-ambiguous grazing rays.
The largest nominal hit-distance difference, 0.52024, was a change in which
stick became visible at a grazing ray; it is not hidden as a small depth error.

The comparison bounds visibility using the trilinear error bound for a
1-Lipschitz box CSG field (`sqrt(3)/2` sample spacing, plus 0.01 sample for
R16F). It compares first hits against analytically evaluated expanded and
contracted surfaces, and checks actual hit surface distance. Ambiguity inside
that bracket is reported separately. Small sampling-sensitive silhouettes
remain a visual limitation to inspect, not an exact-geometry claim.

The reset/batch matrix uses separate visible boxes and checks every edit ray.
Counts 0, 128, 129, 256 and 257 passed with zero mismatches, exhaustion or
ambiguities, including the final partial batch. CPU recipe tests also pass.
The windowed lifecycle smoke passes under Xvfb on the native Vulkan device,
including resource creation, a rendered frame and shutdown. This does not
establish manual pointer-control or resize acceptance on the physical desktop.
Later lighting/playback/performance results will be recorded as completed.

## Shared lighting and graph checkpoint

The SDF pass imports the field into the render graph and writes transient
linear HDR color plus real depth. Shared `HdrPostFrame` applies the single final
display transform. The atmosphere runtime supplies the same direct light and
diffuse irradiance used by the object and ground; the shared atmosphere GLSL
integrator supplies the sky. No reflection probe, night-sky assets or terrain
are required by this material. Its specular highlight uses the shared GGX
helpers, and environment lighting is diffuse SH.

Hard/soft shadow and optional local-AO captures completed with required Vulkan
validation. Secondary-ray exhaustion also appears as magenta. The native
windowed smoke passes under Xvfb after the graph integration. The cut oracle
and crossing raster depth fixture were rerun. The shared sky backdrop horizon
policy removed a dark strip between the sky and the local receiver.

Supersampling at twice each dimension was inspected on the 512- and 2,048-stick
images. A higher view (`pitch=0.53`, `distance=1.75`, `exposure=0.7`, sun azimuth
`-35`) gave a more readable object/shadow composition; final capture framing is
still being refined.

Initial isolated timing: 2,048 sticks, seed 42, atlas 92, 1920x1080, default
camera, soft shadows, AO off, supersampling 1, 80 headless render-only frames,
10 warmup frames, required validation. No readback or encoding was active.
The profiler retained 68 completed GPU samples: trace median 12.526 ms,
p95 12.928 ms; post median 0.0135 ms. This is an initial measurement, pending
the matched final matrix and construction-stage breakdown.
