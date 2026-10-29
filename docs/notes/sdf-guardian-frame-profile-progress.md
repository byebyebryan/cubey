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

## Completed execution

- Ordinary current candidate remeasures at 51.19–51.56 ms GPU p95. Combined
  debug-off / eligible-one-material specialization measures 20.84–21.01 ms.
- Six full-shading controls produce identical PNG hashes in rear and side
  full-HD views and the preset 320p view: eighteen native captures.
- Driver compiler statistics report 128 to 96 temporary registers and 427,648
  to 256,896 bytes of machine code. Local-memory-size values fail the trivial
  vertex-shader sanity check and remain excluded; no live occupancy, spill or
  cache-counter claim is made.
- All 155 independent geometry checks pass; five invalid uses reject. All
  78 existing SPIR-V binaries and 138 protected JSON files retain their hashes.
- The accepted matrix has 51 runs / 7,650 completed GPU samples. Forty-six
  runs have zero encoder activity at both boundaries; five late runs observe
  background encoding. Every accepted run is retained, including the slower
  third specialized repetition. No other owned benchmark/build/test ran alongside it.
- Seven CPU, three documentation and the shader build-rule check pass. The
  [findings](sdf-guardian-frame-profile-results.md) recommend production-safe
  shader separation/selection before more CSG hierarchy work. No default changed.
- The [HTML report](http://starship:8787/reports/3e679d6dca5e9ebe020d7915f42058d47b1e9df5c5d0727880f39ba07546c890/)
  is published on the existing shared server. All seventeen bundle files match
  HTTP hashes; desktop/mobile and all three image-view controls pass, with no
  invalid numbers, broken images or console errors. Temporary viewport reset.
