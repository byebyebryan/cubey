# Ray tracing first pass results

Date: 2026-11-04. Native runtime: `92bc389d`.
Branch: `raytracing-first-pass-20261104`, isolated in
`/home/bryan/code/cubey-raytracing-first-pass`.

[Native captures and interactive evidence report](http://starship:8787/reports/5b8e1c574ac60fe62d926bcd82949a7d173bccd3c9910be6ecf9a0ac9a886880/).
HTTP entrypoint and all 33 bundled files have verified bytes. Browser
interaction acceptance remains open: the available automation failed before
navigation. Local links/assets and JavaScript syntax checks passed.

## Delivered behavior

The [raytracing application](../../projects/raytracing/README.md) renders the
Stanford dragon above a floor using Vulkan KHR triangle ray-tracing pipelines.
It includes static BLAS/TLAS builds, an explicit shader binding table, direct
diffuse/GGX response, hard shadows, constant ambient fill, six view modes,
orbit/zoom/reset, saved configuration and deterministic headless captures.
The [first-pass contract](raytracing-first-pass-plan.md) is implemented within
the qualification limits below. SDF intersection work remains the next stage.

Device/host requests and address allocations are opt-in. Shader target selection
is per application. RT tracing and AS builds stay explicit, with the trace image
imported into Cubey's existing HDR post graph. Sync validation exposed and led to
fixes for acquired-image stage ordering in this renderer and the shared ImGui
attachment-load access mask. No generic RT backend or bindless service was added.

The pinned Khronos donor ran unmodified with external X11 capture. Native
triangle parity has zero hit-mask disagreements across 48,984 regular pixels;
maximum barycentric RGB error is 0.002188 against the preselected 0.005 tolerance.
The vertical image adapter is recorded separately from later lighting/post.

The pinned archive member actually contains **437,645 vertices and 871,414
triangles**, rather than the larger repository catalog count quoted in the plan.
Its 108 zero-area triangles remain in the source index stream. Offline preparation
preserves connectivity and records a uniform normalization and area-weighted
smooth normals. The prepared GLB SHA-256 is
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.
Stanford terms and donor Apache attribution are retained separately.

## Qualification

The dev preset selected 275 groups: 273 passed initially, and the two failures
passed after synthetic test fixtures stopped depending on a retained SynxFlow
environment/release binary. These were test-only fixes. This is full-run plus
focused-rerun coverage, not a single all-green full-suite invocation. All six
RT groups passed again at the measured runtime source without skips.

Independent double-precision triangle tests scored 49,152 rays over four cameras,
including transformed geometry and a grazing view. Maximum depth error is
2.059e-6 scene units and barycentric error 2.609e-6, both below 2e-5; maximum
normal angle error is 3.195e-6 degrees against 0.1 degrees. Hit masks and instance
IDs agree; there are zero scored shadow disagreements. Those tests used
`407d7922`; the later measured source changes metric labeling/log screening only,
with identical RT SPIR-V.

Owned-Xvfb checks exercised orbit/zoom, saved state, reset, shadow/material/light
edits, diagnostics, resize/restore and shutdown with Vulkan/sync validation.
AS builds stayed at three while target generations reached three. Saved camera
reload agrees with the external window capture within one 8-bit channel value
over 456,000 pixels outside the UI. Scene/pipeline/target partial failures and
retry pass. The ordinary non-RT device and disabled-address rejection were
executed; rejection on an actual unsupported GPU remains unexecuted.

Representative SDF observatory and mesh-PBR clay/block decoded RGB captures
are byte-identical between the original release binaries and the isolated
build at 640x480. Both paths were validation-clean. This is a bounded regression
check, not renewed qualification of every pre-existing presentation.

## Measured costs

RTX 5070 Ti, driver 610.57.04; optimized RelWithDebInfo build. Each of six
configurations has three serial runs at 1920x1080, 180 frames, 30 warmup and
150 completed samples per pass per run. The range below spans the three p95s.
Normal routes used timestamp profiling with validation disabled. No owned
build/test/capture/encoding work overlapped timing; boundary GPU observations
are retained but do not establish continuous shared-host exclusivity.

| View | Shadows | Trace p95 ms | Whole-scene p95 ms |
| --- | --- | --- | --- |
| hero | on | 0.183–0.183 | 0.197–0.197 |
| hero | off | 0.135–0.135 | 0.149–0.149 |
| opposite | on | 0.175–0.176 | 0.188–0.190 |
| opposite | off | 0.132–0.132 | 0.145–0.146 |
| detail | on | 0.207–0.208 | 0.221–0.221 |
| detail | off | 0.154–0.154 | 0.167–0.167 |

All configurations pass the proposed 16.7 ms GPU screen. These tiny static scenes
and simple direct lighting do not predict a path tracer's cost or present/UI FPS.
Whole-scene includes tracing and HDR post; readback, PNG encoding, presentation
and physical latency remain separate.

Three separately validated final-image runs measured CPU scene preparation
23.97–25.10 ms, GPU upload/build submission wall 22.73–25.46 ms, AS submission
wall 3.44–3.70 ms (included in upload/build), actual AS GPU work 1.70–1.78 ms,
and pipeline setup 14.11–14.90 ms. Pipeline caches were already warm; cold-driver
startup is not established. All nine process-launch-to-completed-PNG runs took
0.55–0.66 seconds, including device/host startup, rendering, readback and encoding.

At 1080p with one headless frame slot, requested geometry is 24,462,080 bytes,
AS storage 53,899,904, SBT 320, HDR target 16,588,800, camera uniform 144 and
disabled-query placeholder 64. Build scratch peaks at 12,501,888 bytes and is
released before steady rendering. Resident and peak simultaneous **project
requested** bytes both reach 94,951,312 (90.55 MiB) on this path. Windowed slots
and overlapping resize targets are counted separately in their receipts.
This scope excludes upload staging, host output/capture targets, allocation
padding and pipeline/descriptor internals; it is not a process-wide VRAM peak.

Vulkan reports 138,870,784 bytes (132.44 MiB) of current-process device-local
heap usage at capture shutdown, an estimate and not a peak. This follows the
[memory-budget definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html).
The separate `nvidia-smi` readings cover the device globally, including other
applications, and cannot be attributed to Cubey alone.

## Review and retained boundaries

Agent inspection of all three final views and normal diagnostics found credible
orientation, complete intended framing, contact shadows and no pervasive acne.
The close-detail preset intentionally crops the body; geometric normals retain
source faceting while shading normals interpolate. Hard visibility transitions
and single-sample silhouette aliasing remain visible. There is no AA, GI,
environment reflection, soft shadow, animation or denoising in this first pass.
Human artistic acceptance and physical desktop controls/latency remain open.

The [small evidence index](../evidence/raytracing-first-pass-2026-11/README.md)
joins identities, commands, numerical/window receipts, captures, cost summaries
and retention. The original 11 protected SDF/horse files match their
pre-loop hashes. Source push, merge and public demo-site publication were not
performed. Keep the isolated branch and ignored native inputs/evidence.

Next: decide a solid-preparation policy for the dragon's holes, produce and
qualify its sampled SDF, then compare mesh RT and SDF traversal using the frozen
camera/material/light contracts. Progressive light transport follows that
representation comparison as its own milestone.
