# Guardian current-candidate frame profile

Date: 2026-10-28. Baseline: `76265c4` on `sdf-tracing-comparison`.
Scope: explain the measured approximately 51 ms full-HD rear view of the
axis-64 root-plus-far analytic candidate. No new acceleration structure,
geometry change, default adoption or GPU builder is part of this investigation.

## Questions and evidence gates

1. Remeasure the ordinary candidate with current GPU/encoder load recorded.
2. Separate camera tracing, geometric normals, material sampling and shadows
   using matched runtime and compile-time diagnostic stages. These are ablations,
   not additive per-stage GPU timestamps. Retain ordinary/full controls.
3. Inspect driver-generated pipeline executable statistics and representations
   through a separate Vulkan diagnostic process. The engine device contract and
   timed ordinary shaders stay unchanged. Register/local-memory statistics are
   compiler resource evidence, not measured live occupancy or bandwidth.
4. Use the existing camera workload as query-count evidence; identify hit/miss
   and long-ray tails. Do not turn 320p visit counts into 1080p timing shares.
5. Test a narrowly chosen diagnostic hypothesis only after the preceding
   results. Any changed query implementation must pass the frozen geometry
   checks and matched native capture before its timings count.

The driver advertises `VK_KHR_pipeline_executable_properties` and
`pipelineExecutableInfo`. Nsight Graphics/Compute/Systems and RenderDoc are not
installed. No driver/tool installation or system setting change is assumed.
Raw receipts: `outputs/sdf/guardian-frame-profile-20261028/`.

## Acceptance and stop

Use three interleaved repetitions, ordinary whole-scene GPU p95 and complete
tail samples. Keep the fixed Guardian rear camera, source hashes, epsilon,
lighting and source/cache identity. Preserve historical shader binaries and
accepted content. Qualification/captures/counters/compiler inspection occur
outside timing runs. No simultaneous owned CPU/GPU work during measurement.

Finish with a causal account supported by the collected evidence, explicit
unresolved hardware questions, and a bounded next recommendation. Keep changes
local; do not push or change defaults.
