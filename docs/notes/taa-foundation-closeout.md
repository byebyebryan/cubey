# Native TAA foundation results

Date: 2026-11-17 implementation, with qualification completed on 2026-11-18 UTC.
This closes the [foundation plan](taa-foundation-plan.md) as an opt-in feature.
No default, public media, deployment or RT sampling policy changes here.

`cubey::render::TemporalAntiAliasing` now owns one native HDR resolve and one
submission-driven history implementation. `ForwardPbrRenderer3D` and the
independent SDF renderer both consume it before their existing HDR display
passes. The forward adapter adds stable-identity rigid-object vectors; SDF
supplies hit depth and content generations. The [architecture contract](../architecture/temporal-aa.md)
defines motion, jitter, validity, resets and retirement.

## What the demonstrations establish

The approved observatory is the main visual comparison: Off+SS1, TAA+SS1 and
Off+SS2 retain the same geometry, materials, Balanced tracing, clear atmosphere
and camera sequence. The 128 submitted views cover stationary ages 0–64,
orbit, stop, return and a deliberate cut. Separate intact/section comparisons
use the approved cutaway camera. The first post-cut image matches a fresh TAA
seed; AA-off pixels match the pre-integration baseline exactly.

Settled TAA reduces stair steps on the rails, dome and fine trim. Against the
imperfect SS2 spatial reference, rail/step luma RMSE falls from 9.50 to 5.49;
the orbit residual-change measure falls from 12.72 to 10.29. Fine instruments
and some moving edges become softer than SS2. These measurements do not make
blur an automatic quality pass, and the SDF cache still limits geometric detail.

The opaque workshop provides the interactive companion. A moving clay sphere
crosses a wood board and bronze bars, stops and returns; all 211 submitted
frames retain consecutive history ages. Woven-cloth camera motion supplies a
procedural-detail control. Agent visual review found smoother silhouettes and
no persistent sphere trail in the inspected crossing/stop/return frames. This
is bounded visual evidence; user aesthetic acceptance and default promotion
remain separate decisions.

## Cost and limits

Serial 1920×1080 GPU measurements on the RTX 5070 Ti, driver 620.70.06:

| Span | Median | p95 |
| --- | ---: | ---: |
| Forward camera motion + rigid vectors + resolve | 0.090 ms | 0.114 ms |
| Forward complete scene, Off | 0.227 ms | 0.245 ms |
| Forward complete scene, TAA | 0.331 ms | 0.383 ms |
| SDF camera motion + resolve | 0.082 ms | 0.185 ms |
| SDF complete scene, Off+SS1 | 466.649 ms | 489.708 ms |
| SDF complete scene, TAA+SS1 | 465.940 ms | 487.384 ms |
| SDF complete scene, Off+SS2 | 1418.928 ms | 1446.894 ms |

Forward uses 448 measured frames after 64 warmup frames; SDF uses 72 after 24.
Validation, encoding and readback are disabled for these runs. The small SDF
whole-scene median difference is run variation, not a claimed speedup in the
trace. TAA easily meets the proposed 1.5 ms motion/resolve budget, but the
observatory's existing trace remains below real-time speed.

At 1080p, history requests 31.64 MiB and its dedicated Vulkan image memory
requirements total 33.75 MiB. Motion/resolved transients request 23.73 MiB per
CPU slot, with measured requirements of 25.31 MiB. Two-slot uniforms request
960 bytes and require 1024. These exclude descriptor/pipeline storage and
whole-process peak residency.

Admission is completed opaque SDF scenes and opaque rigid forward geometry
under static appearance and clear sky. Diagnostics, SS2+TAA, glass/transmission,
masked/blended materials, deformation, clouds and dynamic displacement bypass
or remain unqualified. DLSS, upscaling, sharpening and denoising are deferred.
The next useful adoption is an explicitly admitted static opaque glTF asset;
terrain needs its own adapter. Ocean, fluid and RT need producer contracts
before adoption. No such rollout is included here.

## Qualification and preservation

The [evidence packet](../evidence/taa-foundation-2026-11/README.md) records exact
source/binary/shader identities, reference parity, commands, native lifecycle
checks, cost and repository gates. CPU and actual GPU fixtures cover jitter,
motion signs, sky translation, partial groups, fast-vector quantization,
one/two/three slots, independent views, discarded recordings, failed creation
and replacement, retry and reset. The forward fixture exercises rigid history,
strict admission and depth-state rollback. Host tests prove submitted/discarded
callback ownership. Native toggle/reset/resize checks use owned virtual X11
windows; physical desktop cadence is not certified.

A final test-only resolve probe reads the actual forward motion field through
the display path: stationary red is 0 and right-moving red is 130 with a static
camera. Camera-only vectors, missing rigid coverage or reversed horizontal
motion cannot pass that control. Production shaders and captured binaries are
unchanged by this additional fixture.

The donor is the explicitly MIT-licensed public Donut shader at
`52efbe37d7f550ea1c5c7ee02b75404ec7c72c09`. A separately compiled HLSL oracle
and Cubey GLSL agree within maximum scaled error 0.001789 against tolerance
0.005 over 24 accepted frames. The original plan incorrectly described the
bundled DLSS sample source as MIT; that was corrected before adopting code.
The sample executable remains comparison evidence only. Normal builds need
neither DXC nor a DLSS SDK. Exact numerical thresholds were fixed during
fixture qualification rather than P0; the port was not tuned to that result.

Work remains isolated in `/home/bryan/code/cubey-aa`. Main and the RT worktree
retain `211903e8`. The SDF/horse study remains parked at `7ae104ee`, with its
bundle, original patch/archive and evidence manifest in the original checkout's
ignored parking directory. This milestone does not reopen that study or the
certified dragon/material work. Disabling TAA selects the original path;
an application that never requests it allocates no TAA history or motion images.
