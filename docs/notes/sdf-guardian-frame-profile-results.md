# What costs Guardian's 51 ms?

Date: 2026-10-28. Baseline: `76265c4`. Diagnostic implementation: `da8f6c6`.
Scope: the existing root-plus-far analytic route, region axis 64, Guardian rear
camera at 1920x1080. This is a completed investigation, not default adoption.

[HTML report — What costs 51 milliseconds?](http://starship:8787/reports/3e679d6dca5e9ebe020d7915f42058d47b1e9df5c5d0727880f39ba07546c890/)
includes matched controls, stage comparisons, native images and source receipts.

## Main finding

The general shader's compiled execution cost is a major problem alongside
the remaining distance-query work. Specializing two facts already true for
this scene—debug mode is zero and the existing one-material shortcut applies—
reduces whole-scene GPU p95 from **51.19–51.56 ms to 20.84–21.01 ms**.
All six full-shading controls produce byte-identical PNGs in the tested rear and
side full-HD views and preset 320p view (18 captures).

This change retains the authored source, regional programs and far certificates,
hit/normal tolerances, material detail, shadows and lighting. It adds no spatial
refinement or approximation. It demonstrates that source/CSG visit counts alone
did not explain the 51 ms frame.

The specialized diagnostic still misses 16.7 ms. Its eligibility is intentionally
restricted to static authored headless atmosphere rendering with one material,
the existing material fast path, debug 0 and no editing actions. It is not safe
to promote by copying a shader filename into a general editor path.

## Controlled full-shading comparison

Three interleaved repetitions, 180 frames, 30 warmup, 150 completed GPU samples
per run. No other owned CPU/GPU work ran during the accepted matrix. Current
GPU/encoder utilization and clocks are recorded at each run boundary. Forty-six
runs have 0% encoder readings at both boundaries; five late third-repetition
runs observe encoder activity. The specialized result remains 20.84–21.01 ms
across all three repetitions. These shared-machine snapshots do not measure
continuous occupancy or qualify an exclusive machine.

| Full-shading control | Whole-scene GPU p95 | Registers | Machine code |
| --- | --- | --- | --- |
| Ordinary general shader | 51.19–51.56 ms | 128 | 427,648 bytes |
| Compile out debug views | 39.61–39.87 ms | 128 | 376,832 bytes |
| Compile in one-material shortcut | 50.75–50.98 ms | 96 | 325,120 bytes |
| Both specializations | **20.84–21.01 ms** | **96** | **256,896 bytes** |

The approximately 59% frame reduction and interaction between the two changes
are measured. Register count drops 25% and executable size about 40%. They are
not sufficient to divide the gain into exact occupancy, spills, instruction-cache
or scheduling percentages. No live hardware-counter attribution is claimed.

The ordinary post pass remains approximately 0.011 ms. The approximately 51 ms
is in the main trace/shading draw, not in cache construction or edit publication.
See the [per-run timing receipt](../evidence/sdf-guardian-frame-profile-2026-10/timing.json).

## What the stage controls show

Runtime stages use one diagnostic shader; fixed stages compile each stopping
point separately. Each table entry is a whole-scene ablation p95 range, not an
additive per-stage timestamp.

| Stop after | Runtime branch | Compile-time stage | Fixed-stage registers |
| --- | --- | --- | --- |
| Camera tracing | 25.37–25.46 ms | 15.60–15.70 ms | 39 |
| Camera plus geometric normal | 26.57–26.73 ms | 15.98–16.12 ms | 48 |
| Camera, normal and material sample | 26.48–26.72 ms | 24.92–25.03 ms | 128 |
| Camera, normal and shadow | 35.66–35.82 ms | 20.22–20.28 ms | 48 |
| Full shading/background | 51.30–51.49 ms | 51.08–51.31 ms | 128 |

The primary-only diagnostic is faster when unrelated code is compiled out.
Adding the material stage raises the register count and frame cost even though
Guardian takes its one-material runtime shortcut. The specialized full-shading
shader is faster than that general material-only diagnostic. These comparisons
demonstrate compiler/context coupling; do not subtract these rows into a pie
chart of supposedly intrinsic operation costs.

Changed-output controls provide further context:

| Ordinary-route diagnostic | Whole-scene GPU p95 |
| --- | --- |
| Shadows disabled | 27.12–27.22 ms |
| Procedural detail and bump disabled | 51.33–51.46 ms |
| Background sky work removed in a separate shader | 38.80–38.98 ms |

Shadow work is substantial in the original shader. Executed procedural detail
does not explain its large frame cost. Removing sky code also changes compiled
resources, so that delta is not a standalone sky timer.

## Source mechanism and remaining work

`regions.glsl` evaluates regional distance programs through a float stack.
The general `authored_material.glsl` evaluator also contains a 32-entry
`SceneSurface` stack, per-material samples and full CSG/material provenance.
`sdf_surface.glsl` retains source-material debug views selected by a runtime
uniform. A normal Guardian draw skips those debug paths and uses the one-material
fast path, but the generic program still affects compiler allocation and code
generation. Both specialized conditions remove those paths from compilation.

The separate 320p rear-view replay still gives approximately 9.4 exact camera
distance evaluations, six normal samples and 12.7 shadow evaluations per visible
hit. Visible-hit programs average 25 instructions, p95 95; the earlier mean of
78 refers to the preset view. Long tails reach 551 camera iterations and 270
shadow iterations. Far bounds remove many CSG evaluations while adding lookup
and advance work. These are workload facts, not full-HD timing shares.

The compact camera/normal/shadow controls and the specialized full-shading result
leave real query work to improve. They support examining that approximately
21 ms path after production shader selection is made correct. They do not show
that changing the authored representation or immediately adding a finer grid
is necessary to address the original 51 ms.

## Compiler statistics and tool limits

Nsight Graphics/Compute/Systems and RenderDoc were not installed. The driver
advertises `VK_KHR_pipeline_executable_properties`. A separate compile-only
Vulkan tool creates matching-format graphics pipelines from the saved SPIR-V
and asks for [pipeline executable statistics](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineExecutableStatisticsKHR.html).
It does not change the renderer's enabled device features.

Register and executable-size statistics are retained as compile-time resource
evidence. The driver returns no internal representations through this interface.
Its local-memory-size statistic reports an implausible value even for the trivial
vertex shader, including after explicit zero initialization of every output
union byte. The raw values are retained and excluded from conclusions; no
reinterpretation of their low bits or claim of measured register spilling is made.

## Qualification and preservation

- 155 frozen independent geometry checks pass. The existing query kernels and
  all 78 historical SPIR-V binaries retain their hashes.
- Six full-shading controls have identical PNG hashes in each of three views:
  rear/side 1920x1080 and preset 320x180. No image downsampling or comparison
  tolerance was needed for this claim.
- Five invalid configurations reject before renderer allocation, including
  multi-material specialization, disabled shortcut, debug mode, missing stage
  and use of native frame profiling in the query harness.
- All 138 protected content JSON files remain unchanged.
- The first new runtime profile hit an uninitialized-buffer logging access when
  combined with the acceleration route. It was corrected by leaving acceleration
  logging to its existing post-upload logger. That failed setup and the short
  pilot are excluded from accepted timings; their receipts are retained.

These captures compare shader specialization against the existing root-plus-far
route. They do not remove the earlier native boundary-pixel limitation relative
to a different tracing control, nor constitute general production/editor adoption.

## Next recommendation

Prioritize proper shader/pipeline separation before another CSG backend change:
separate debug views from the common lit shader, and make the eligible material
shortcut a validated compile-time/pipeline choice. Preserve the general evaluator
for scenes that need it. Pipeline selection must track material/appearance
changes, debug selection and undo/publication, with multi-material Robot and
optical routes qualified before adoption.

Then profile the remaining approximately 21 ms path and decide whether finer
regional programs, improved traversal or another shader split earns its cost.
A GPU builder addresses construction latency; it cannot remove shader execution
cost by itself. This investigation implements neither production selection,
finer regions, GPU construction nor a default switch.

See the [evidence index](../evidence/sdf-guardian-frame-profile-2026-10/README.md),
[execution scope](sdf-guardian-frame-profile-progress.md) and
[earlier empty-space comparison](sdf-acceleration-loop-results.md).
