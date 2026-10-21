# SDF implementation evidence

Date: 2026-10-21. Native V1 implemented under the
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

## Recipe persistence checkpoint

Version-1 JSON retains all box transforms, operations and dimensions, including
the partial tail beyond a 128-edit boundary. CPU roundtrip checks cover a
257-stick list, ordered subtraction and an empty recipe; corrupt hashes fail.
A native 257-stick save/reload produced the same explicit recipe hash
`4eed14fe9a9ec1f0`, edit count and 88,474 active bricks.

The before/reload PNGs were not byte-identical (RGBA PSNR 72.29 dB). Repeating
the loaded recipe also differed by a similar amount (72.37 dB). GPU append
order changes atlas addresses, so hardware texture-coordinate interpolation
can affect grazing contacts and shading. Of 307,200 pixels, 48 differed on
save/reload: 44 by one channel level, two by two, one by seven and one by 57.
The repeated loaded run also had one pixel differing by 57. Geometry/timeline reproducibility is
qualified within sampling tolerance, not bit-identical frame buffers.

## Timeline and publication checkpoint

The native 257-stick construction video crossed full batches and ended at the
same 88,474 bricks as a fresh full build. CPU timeline checks cover the first
frame, partial tail, construction endpoint and both diagnostic stages, including
the same logical states at 30/60 FPS. Headless camera/view state is derived from
the submitted frame, so queued video recording does not read a later mutable
timeline sample.

The extended native matrix passes forward, unchanged and backward prefixes,
reset, and both 64- and 128-edit batches with individually visible edits.
Deliberately reduced frontier and edit-reference buffers each reject their
overflow before publication, then recover to a validated empty field.
Required Vulkan validation emitted no errors in these runs.

Construction now records GPU filtering and sampling/copy dispatch times,
recomputed/copied brick counts, and remaining wall time. An isolated full
2,048-stick build measured 129.9 ms: 65.4 ms filtering, 50.4 ms brick work and
14.1 ms other wall time. Across its 16 batches, 4,838,910 bricks were copied and
712,716 recomputed (work totals, not simultaneous atlas occupancy). This does
not isolate copy-only time from sampling; both share the donor-style dispatch.
The measured cost does not justify an indirect-dispatch foundation change in V1.

## Final native qualification

[Published native review](http://starship:8787/reports/7f1fd699ff363f13294391b0917eac5e1ba9e98f348aaedced78de93d4142211/)
contains the video, stills, comparisons and raw receipts. HTTP delivery was
verified, then native video playback, phase seeking and the lighting selector
were exercised in Chrome. The export includes a browser verification image.

Measured native implementation: `651fd2c2b93e7e6e7091339cbd1eead562585178`.
The later tools/documentation checkpoint preserves the capture recipes and
review export; it does not change the measured rendering implementation.
The capture manifest records the executable hash, applied configurations,
source hashes and individual results; `runtime-artifacts.json` additionally
records the SPIR-V hashes. The executable was rechecked against the manifest.

Device: NVIDIA GeForce RTX 5070 Ti, driver 610.57.04, 16,303 MiB inventory.
Each fixed timing run used 180 headless render-only frames at 1920x1080,
30 warmup frames and 148 completed GPU samples. The final two frame slots were
not collected. Readback/encoding were disabled, validation was required,
supersampling was 1, and the full 2,048-stick field used atlas profile 92.
Camera: yaw 0.6, pitch 0.3, distance 1.35, target height 0; sun elevation 35,
azimuth -55, exposure 0, roughness 0.36. The field was built once.

| Mode | Median trace + post | p95 | Render-only loop, 180 frames |
| --- | ---: | ---: | ---: |
| Primary + atmosphere | 3.052 ms | 3.199 ms | 0.552 s |
| Hard shadows | 5.322 ms | 5.549 ms | 0.964 s |
| Soft shadows, four samples | 12.563 ms | 13.062 ms | 2.268 s |
| Soft shadows + local AO | 14.568 ms | 15.104 ms | 2.629 s |

The selected static modes meet the planned 60 FPS primary/30 FPS lit timing
targets on this device at 1x sampling. These results exclude geometry rebuilds
and do not qualify 1080p 2x supersampling or other devices.

The authored clip completed as 720 H.264 frames at 1280x720/30 FPS, duration
exactly 24 seconds. It uses 2x internal resolution, the committed stick-ball
preset, eight seconds of construction, twelve of orbit, then traversal and
brick-cell diagnostics. Five decoded stages were inspected, and browser
playback/phase seeking were checked. The static host lacks HTTP Range support;
the review player downloads a local Blob to make seeking work without changing
the report server. Lighting comparison controls and relative assets were checked.

The complete capture matrix includes transformed/subtracted box normals,
opposing raster depth views, batch/overflow checks, matched lighting modes,
a partial construction frame, diagnostics and a second seed. Required Vulkan
validation emitted no errors. The full stick-ball oracle reported zero
out-of-bound errors/exhaustion and four sampling ambiguities in this capture.
The windowed interaction exercise injected orbit, scroll, pause/reset,
two resizes and Escape in an isolated Xvfb display; it completed with one field
build and clean shutdown. Physical-desktop/user aesthetic acceptance is pending.

Current limits remain explicit: 2,048 sticks use 630,656 bricks and
1,974,880,928 bytes of field resource payload. The 6,400-stick stress case
exceeds the 778,688-brick profile and fails without publishing a capture.
The field allocation counts exclude frame targets, driver overhead and padding.
Terrain backgrounds, desktop sculpting, learned generation and GI remain
exploration candidates rather than V1 dependencies.
