# Guardian source-query investigation

Date: 2026-10-28. Baseline: `0662f40c` on `sdf-tracing-comparison`.
Diagnostic source candidate: `7b9ceb4` (committed locally after measurement).
Status: bounded investigation complete; diagnostic routes remain opt-in.

Guardian has both avoidable implementation overhead and a substantial remaining
CSG-query workload. The measured bottlenecks are primary tracing and hard
shadows. Material detail and the six-sample geometric normal stencil are smaller
costs in this view. These results support keeping the source/history contract
and investigating cheaper empty-space traversal and local programs next.

The [evidence pack](../evidence/sdf-guardian-investigation-2026-10/README.md)
contains the measurements, commands, qualification results, source/shader hashes
and native images. This follows the
[tracing comparison](sdf-tracing-comparison-results.md); it does not select a new
default, change accepted content/history, or implement a production accelerator.

## Measured frame cost

RTX 5070 Ti, driver 610.57.04; Release, 320x180, 1x, accepted preset cameras,
fixed atmosphere lighting, one hard shadow, no AO/ground. Values below are the
range of per-run whole-scene GPU p95s across three interleaved repetitions.
Region axis is 64. All rows in this table use the same diagnostic full shader.

| Guardian buffers / tracing box | Analytic | Hybrid |
| --- | --- | --- |
| Original system-memory geometry buffers, outer box | 14.00–14.08 ms | 17.79–17.81 ms |
| Original buffers, padded inner box | 12.56–12.58 ms | 16.40–16.43 ms |
| GPU-local region buffers, outer box | 11.70–11.71 ms | 15.23–15.27 ms |
| GPU-local source and regions, outer box | 10.93 ms | 14.40–14.44 ms |
| GPU-local source and regions, padded inner box | **10.132–10.133 ms** | **13.58–13.62 ms** |

The combined ablation reduces the analytic diagnostic p95 by about 28%, and the
hybrid p95 by about 24%. That is a useful gain at a small resolution. Guardian
candidate full-HD performance is still **not measured**. No extrapolation or
60 fps claim follows from these rows.

For context, the unmodified renderer measured 14.54–14.66 ms analytic and
17.54–17.67 ms hybrid in the initial 320p stage matrix. Diagnostic branches change
compiler behavior: its full analytic control measured 13.97–14.03 ms. Therefore
use matched diagnostic controls for ablation gains rather than subtracting a
candidate from the ordinary shader. Robot's shorter runs also vary between
measurement blocks; small cross-block deltas are not optimization evidence.

The residency matrix uses 180 frames, 30 warmup and 150 completed GPU samples
per run. The stage matrix uses 80 frames, 16 warmup and 64 completed samples.
All tail slots are present. There are 324 accepted timing runs. Counter-shader
elapsed times are excluded from performance results.

## Primary tracing and shadows dominate

These early-exit stages retain preceding geometric work. Material and shadow
stages are separate branches after primary tracing and normals; they are not
successive additive timers. Misses use the same cheap background in all
partial stages; the full stage retains normal background lighting and post.

| Guardian stage, original buffers and outer box | Analytic p95 | Hybrid p95 |
| --- | --- | --- |
| Primary only | 8.67–8.79 ms | 9.61–9.65 ms |
| Primary plus geometric normals | 9.15–9.17 ms | 9.98–10.04 ms |
| Primary, normals and material sample | 9.11–9.16 ms | 9.99–10.05 ms |
| Primary, normals and hard shadow | 12.60–12.65 ms | 15.63–15.68 ms |
| Full diagnostic | 13.97–14.03 ms | 17.69–17.74 ms |
| Ordinary full shading with shadows disabled | 9.55–9.69 ms | 9.87–9.94 ms |

Disabling procedural material detail/bump leaves ordinary full shading around
14.60–14.73 ms analytic and 17.60–17.75 ms hybrid. Guardian has one material and
uses the existing single-material shortcut. The original suspicion that a full
material-expression walk might dominate Guardian is not supported. Robot has
multiple materials, but its material ablation is also a small part of its frame.
Differences of a few hundredths of a millisecond between partial stages should
be treated as variation/compiler effects, not negative component costs.

## Boundary entry triggers full-source work

The ordinary tracer intersects the region box at half extent 0.5. Reconstructing
its first point in FP32 sometimes places that point just outside the box.
The region evaluator correctly falls back to the complete source outside its
qualified domain. In the matched Guardian replay this occurs 5,410 times, with
maximum excess `1.1920928955078125e-7` workspace units. The complete program has
2,021 instructions, so those calls account for 50.7% of primary CSG instruction
visits, despite being only 3.1% of primary distance queries.

These are outside-domain fallbacks, separate from reference-capacity exhaustion.
All measured content has zero region-capacity fallback cells. Removing the
outside-domain safety fallback is not the proposed fix.

The diagnostic intersects a padded box at half extent **0.4995** instead. The
actual source is already clipped at 0.499, leaving 0.0005 of empty margin, and
initial points remain inside the 0.5 region domain. This eliminates observed
full-source calls in the accepted padded replay while preserving the fallback.
The time gain is smaller than the instruction-visit reduction: CSG visits are
not GPU machine instructions or a linear prediction of execution time.

An earlier exact 0.499 interval was rejected. The unchanged `workspace-clip/1`
inside-exit fixture became a hybrid miss: a cached step ended
`4.190951585769653e-9` beyond the strict interval endpoint. Keeping an empty margin
passes the original tests. Timings for that rejected interval are retained
locally and excluded from accepted candidate comparisons. No tolerance was
relaxed to admit it.

## Geometry buffers were in system memory

`GpuRegionPrograms` requests host-visible/coherent memory, and `Buffer::create`
chooses the first compatible memory type. Live buffer requirements, that same
selection function and device properties establish type 2 / heap 1 / flags 6
for the original region cells, references and native source buffer on this GPU.
Heap 1 is the non-device-local system-memory heap.

The private residency ablation additionally requires device-local memory for
regions, source, or both. It selects type 4 / heap 0 / flags 7. The bytes and
shaders are unchanged. Moving regions helps more than moving only the source.
These results concern allocation placement; hardware cache-miss and PCIe traffic
counters were not collected, so they do not identify a measured bus bottleneck.

Host visibility and device locality are distinct Vulkan properties; this behavior
is consistent with the
[Vulkan memory specification](https://docs.vulkan.org/spec/latest/chapters/memory.html).
A production implementation should use the existing device-buffer upload path
or a qualified compatible memory type, with correct publication/lifetime rules.
The diagnostic's CPU-mappable device-local type is available on this device and
is not a portable requirement. A global allocator policy change is outside scope.

The previous 54.68 MB Guardian region figure describes requested buffer bytes,
not proof that those bytes resided in VRAM. The new allocation receipts make
that distinction explicit. No complete renderer/allocator memory profile is
claimed.

## Remaining workload and the weak hybrid payoff

Counter replay uses the preset camera ray grid and sun direction, with primary,
normal and shadow phases recorded separately. It is a compute replay, not an
instrumented count of native fragment invocations. Rays are generated from the
camera in CPU arithmetic; sparse uninstrumented comparisons qualify the replay.

| At 64 regions per axis | Guardian | Robot |
| --- | --- | --- |
| All-region mean program length, previous comparison | 50.1 | 6.8 |
| Mean program at visible replay hits | 78.0 | 4.9 |
| p95 program at visible replay hits | 195 | 13 |
| Mean primary program after padded entry | 63.9 | 6.1 |
| Primary CSG work on miss rays after padded entry | 60.9% | 67.4% |

Guardian takes slightly fewer total marching iterations than Robot in this view.
Its distance queries do much more work. Its padded analytic replay executes
18.75 million CSG instruction visits across primary, normals and shadows;
primary misses alone account for 6.86 million. These counts suggest tighter
conservative scene bounds and cheap empty-space distance bounds as useful next
experiments, alongside reducing surviving programs near the surface. They do
not establish the speedup of either change.

Hybrid accepts cached steps on approximately 17% of primary iterations and 39%
of shadow iterations. It still evaluates the source in uniform cells because
labels are not distance bounds, and it clips positive sampled steps to brick
cell exits. More iterations and cache work accompany relatively few saved
source queries. In the padded Guardian replay hybrid reduces total CSG visits
by only about 9% while retaining the full 913 MB field. Its measured GPU time
remains higher. This diagnoses this cache's poor payoff; it does not disprove
using cheap conservative distances to guide exact surface evaluation.

Full-Guardian region construction/upload still takes roughly 421–473 ms in the
residency runs. These are cold construction measurements, not new first-completed
edit timings. The previous action-completion measurements remain separately
pinned. Faster construction can address edit latency but cannot by itself remove
the remaining frame-query work.

## Qualification and preservation

- Four padded routes (analytic/hybrid, axes 16/64) pass 155 independent checks
  each: **620 checks** with unchanged fixtures and tolerances, including inside,
  clipping, near-parallel and smooth controls. Supplemental GPU point probes pass.
- **230,400** paired dense camera rays retain classifications and satisfy the
  prior regular/grazing depth and residual contracts. No primary or shadow
  exhaustion occurs. These are differential content checks, not independent
  closed-form Guardian geometry proofs.
- **28,800** sparse uninstrumented comparisons check that counters do not change
  geometry. Histogram totals equal query totals, normals use six source queries
  per hit, and source queries plus accepted cache steps equal trace iterations.
- The first counter implementation was rejected on histogram accounting. Explicit
  component updates pass; the original nested-index update failure was not
  independently diagnosed. Rejected receipts are outside the accepted dataset.
- Sixteen native 320x180 captures cover both scenes/routes and four memory/bound
  combinations. Memory placement alone is pixel-identical. Padded entry is also
  identical except one Guardian hybrid pixel changes one channel by one byte.
  There are zero exhaustion pixels. This is bounded native capture evidence,
  not physical desktop interaction or human visual acceptance.
- Seven CPU SDF tests pass. All **43 pre-existing shader binaries** and **138 scene/
  preset JSON files** retain their exact baseline hashes. The sampled default,
  accepted authored content and history remain unchanged.

## Recommended next decision

1. Consider a narrow adoption pass for safe padded entry and explicit GPU-local
   geometry residency. Preserve the outside-domain fallback and qualify portable
   upload/publication behavior. Keep the sampled default through that pass.
2. Before a larger backend pivot, compare conservative scene bounds and distance
   bounds for empty regions against the current source-query path. Measure saved
   work and native GPU time. Finer/adaptive or more effective pruning should then
   target the remaining near-surface tail; blanket removal of smoothing/groups
   is not supported by these measurements.
3. Treat region construction/edit latency as its own gate. GPU construction,
   partial updates or using pruned programs in brick construction remain options,
   with no performance claim until measured. The current hybrid field's cost and
   limited traversal benefit make its cache design worth revisiting.

These are proposed follow-ups, not adopted implementation goals. Current evidence
supports improving acceleration while retaining complete source/history and exact
surface evaluation as the quality path. More authored content would not resolve
the measured bottlenecks.
