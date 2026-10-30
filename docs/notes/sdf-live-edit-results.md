# Frozen-base live CSG: native findings

Date: 2026-10-30. This completes the bounded stages 1–2 investigation in the
[continuous-editing plan](sdf-continuous-editing-research-plan.md). Source retained
at `72a1a4889176e2e8d8d731cf4fd1abd5831e4063`; private executable and all eight
shader hashes are in the [evidence packet](../evidence/sdf-live-edit-2026-10/README.md).
The [execution record](sdf-live-edit-experiment-progress.md) retains failed pilots
and gate decisions. Public modes, the Balanced default and canonical content are
unchanged. All commits remain local.

[Open the HTML report](http://starship:8787/reports/41999c42e4ba06abdfad59d6ef6b005ca14d98c5c63a9a377123eaa00cc60e51/).

## Decision

Keep retained source/history, a frozen base and an ordered pending expression as
the hypothesis. The native experiment supports the composition, including real
subtractions. It does not qualify the current prototype for continuous editing,
and cache completion does not qualify automatic retirement of the live suffix.

Candidate A composes a sampled base with pending canonical CSG, using source
extensions where uniform labels or workspace clipping do not supply a scalar.
Candidate B composes conservative base intervals with that suffix and resolves
source geometry where clearance cannot be certified. These are private study
policies, separate from the existing public Hybrid mode. Both include edited
normals, hard-shadow rays and canonical material/cut ownership.

B passes 155 unchanged independent source controls and 58 pending controls.
A passes the 58 pending controls; its zero-tail source score retains sub-grid
feature and thin-normal failures. Native camera/shadow replay and maximum-tail
replay have no exhaustion. Four A pixels remain unqualified by native
compute-to-fragment validation: one Guardian classification pixel and three
Robot depth/normal pixels. B has no flagged pixels on these two native views.
Those are bounded observations, not a proof over arbitrary scenes or rays.

## Measured cost

RTX 5070 Ti / 610.57.04, 1080p SS1, opaque atmosphere, hard shadows, fixed preset
cameras. Each row has three interleaved runs of 30 warmup and 150 measured frames.
Values below are median run p95 milliseconds. Complete samples, repeat ranges,
p50/p99/max and every 11.11/16.67 ms miss are in `timing.json`.

| Edited scene | A: sampled base | B: source defined | Fresh Cached | Fresh Balanced | Fresh Analytical |
| --- | ---: | ---: | ---: | ---: | ---: |
| Robot, zero pending leaves | 11.14 | 12.42 | 4.24 | 4.35 | 5.10 |
| Robot, eight leaves | 24.18 | 27.36 | 4.55 | 4.58 | 5.29 |
| Guardian, zero pending leaves | 26.73 | 35.49 | 1.91 | 4.66 | 19.38 |
| Guardian, eight leaves | 33.74 | 43.48 | 1.99 | 4.76 | 19.56 |

Fresh controls bake the same edited scene; their row's leaf count describes the
edits included in that bake. They have no pending suffix. A is a new tightly
stepped sampled-base tracer, not the existing Cached implementation with a tail
added. Its overhead cannot be attributed to cached sampling in general.

At Robot +64, A/B reach 115.48/137.65 ms p95. At Guardian +13 they reach
38.85/48.85 ms. Robot +1 has no 16.67 ms misses in either candidate's 450 measured
frames, but all 450 exceed 11.11 ms. Even zero-tail Robot A has 23/450 misses at
11.11 ms; Guardian fails both deadlines throughout the candidate sweep. There
is no qualified 90 Hz live-tail cap in this study. The timing sweep uses visible
hard-union stamps; long-tail cut/smooth performance remains unmeasured.

Median synchronous frozen field construction is approximately 91.5 ms for Robot
and 920.4 ms for Guardian, before separate source-region preparation. These are
initialization costs, excluded from warmed rendering. They do not establish
updater service rate or schedulable work-unit time. A field requests 912,997,552
bytes; two fields alone request about 1.70 GiB before regions, staging and other
resources. Overlapping-version residency is not implemented or qualified.

## Handoff and motion

The native sub-grid cavity control reproduces the research result. A pending
radius-0.35h cut is visible to both candidates at about 0.0001953 world units
from its center. A fresh ordinary cache fills it; the ray hits the outer body
near 0.1996 instead, with a reversed normal. Ordinary Cached also has looser
termination than the frozen source gate, producing separate approximately
0.00009 regular-ray shifts. The strict sampled handoff gate fails. Balanced's
normal quality cannot restore topology missing from its sampled base.

Full 320x180 geometry payloads compare live surfaces with fresh Cached,
Balanced and an explicitly combined source control. Native validation flags
are excluded and counted. Relative to B, fresh Cached gains six Guardian and
14 Robot foreground pixels in these views; Balanced gains six Guardian pixels
and changes 13 Robot classifications. The fresh source control has no
classification differences against B, with p95 depth differences around
0.000013 and normal differences below 0.04 degrees.

The existing payload guard accepts explicit combined source controls rather
than named Analytical. Those controls are labeled separately. Ordinary combined
and public Analytical images differ at one Guardian pixel by one channel unit;
Robot is identical. Public timing and the 960x540 image gallery still use the
actual named modes. No guard was weakened or equivalence silently assumed.

Each native motion sequence changes camera pose and the pending program across
16 frames, from one to eight leaves, with exactly one base field and one region
build. There are 64 captured sequence frames across subjects/policies. Upload,
frame completion and readback are serialized. Fresh-bake comparisons use separate
native runs; this does not implement concurrent publication or in-process suffix
retirement.

## Next bounded experiment

Resolve the display contract before an updater: either an explicitly sampled
authoring profile with a minimum-feature policy, or a source-faithful profile
that retains analytic evaluation/qualified patches after baking. Do not infer
safe retirement from successful construction.

Next measure and reduce zero-tail overhead and repeated pending work across
primary, normal and shadow queries. Candidate directions are shader/stack
specialization and conservative spatial tail filtering. Query dominance and
interval certificates can support pruning; a primitive's solid AABB is not a
complete exact-value invalidation rule. Keep the existing renderer and the same
geometry gates as controls.

Only after the live renderer and handoff contract pass should stage 3 compare
pruned canonical full construction against stable local patches, including all
CPU/GPU preparation, transfers, setup, memory and work-unit duration. Publication
must then prove exact suffix accounting through new input, undo, redo, branches,
cancel and stale completion. Continuous service/drain, input latency, finite
600-leaf streams and physical XR acceptance remain later gates. Baking does not
free the canonical source budget: full Guardian only admits 13 appended leaves;
the 14th is rejected before GPU allocation.

## Validation and delivery

All 13 focused SDF/documentation/shader checks pass. Original 194 SPIR-V binaries
and 567 protected source/content files are byte unchanged. The report's static
checker exercises all filters, 30 image choices, 32 paired motion states, reset
and playback. The 94 exported images preserve native PNG channels losslessly.
No browser was connected; static interaction and HTTP byte verification do not
establish rendered browser or human visual acceptance.
