# SDF query acceleration loop progress

Started 2026-10-29 from `89c06623`, following the
[research plan](sdf-query-acceleration-research-plan.md). Active local goal;
no push, default change or windowed root-far admission.

## Phase 0: current workload and child pilot

The selected-query diagnostic reuses the actual renderer camera, lighting,
program, material, region and prepared-cone buffers. Its compute kernel calls
the same tracing, material and shadow functions as the selected fragment shader.
A private fragment variant checks sampled status, depth, geometric normal and
required/skipped shadow decisions against the replay. The ordinary shaders
remain byte-identical to their frozen baseline.

The first attempt used computed pixel-center UVs. Comparing independent shader
stages with a 2e-6 depth / 1e-4 normal-vector / tight radiance threshold failed.
Those are stricter than the frozen geometry contract. The final diagnostic
captures raster-interpolated UVs in R32G32 first, uses the existing 4-EPSILON
regular-depth and one-degree normal contracts, and requires exact shadow-call
decisions. Its validation images must also match the ordinary native image
byte-for-byte. No production or fixture tolerance changes.

Sampled full-HD workload uses one pixel per 8×4 tile: 64,800 camera rays, a
16,588,816-byte result buffer and 16,588,800-byte UV image. Total payload is
33,177,616 bytes, below 32 MiB; allocator/driver overhead is separate. Per-ray
32-bit counters are aggregated on the CPU with wider integers. These are query
counts, not GPU time shares, and diagnostic setup/timing is not production cost.

All twelve Guardian/Robot × three views × shadow/combined captures pass native
image parity, frozen depth/normal and exact shadow-decision checks. Each hit has
six normal queries; all records have identity stamps and finite outputs; no
query exhausts. Guardian has zero root-fallback queries in these samples.

| Guardian view | Primary exact queries | Normal exact queries | Required shadow exact queries | Mean primary exact run |
| --- | ---: | ---: | ---: | ---: |
| Preset | 70,852 | 33,228 | 47,760 | 8.81 |
| Side | 73,039 | 30,996 | 56,754 | 9.45 |
| Rear | 72,983 | 33,996 | 18,656 | 8.71 |

Counts are identical between A and A+B in this sample; this does not mean equal
query cost. Required shadow rays are 5,416 / 2,944 / 946, and skipped shadow
calls 122 / 2,222 / 4,720. Maximum primary exact runs are 269 / 216 / 270.
Long exact runs make checked relaxation worth a bounded trial. Normal queries
remain a separate floor and no CSG-visit fraction is called a GPU time fraction.

The CPU pruning pilot selects 256 distinct active parents by program-length
bin and spatial octant, including 225 parents with smooth operators. It builds
16,384 children and checks 245,760 independent full-source points: centers,
corners and the normal stencil. Zero failures; maximum value error or far-bound
excess is 3.73e-9. Pilot child programs have 41,874 references and 11,790 far
cells. Mean length among non-far children is 9.11.

Stratum-weighted forecasts are about 2.17 million new references, 27.17 MiB total
region payload, and 338 ms child construction in addition to the base rebuild.
These are point estimates, without a confidence interval or full-level timing.
They support deferring full refinement under the proposed edit-latency gate;
they do not reject every partial or differently constructed hierarchy.

Guardian's existing authored arithmetic guard is 0.000151686, about 8.7 EPSILON;
the region guard is twice that. Relaxation must subtract a justified guard from
its empty-ball radii and keep ordinary steps when that removes the opportunity.
No smaller empirical error or donor constant is substituted for that audit.

Decision: test fixed checked relaxation before adaptive prediction. Keep the
current automatic A and explicit A+B as independent performance controls.
Full refinement and GPU construction are not started by this checkpoint.

Raw receipts: `outputs/sdf/query-acceleration-loop-20261029/`.
Qualified workload is `workload-qualified/workload.json`; earlier attempts are
retained as failed diagnostics. One attempt launched an older executable after
a failed build while its shader files had already changed; Vulkan validation
rejected the missing descriptor binding. It is excluded. Subsequent native
execution follows a successful build and clean validation.
