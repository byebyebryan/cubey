# Cached commit reuse: qualified results

Date: 2026-10-30. Bounded execution of the
[cache-commit research plan](sdf-cache-commit-research-plan.md).
Implementation and measurements are local to `sdf-tracing-comparison`.
The [evidence packet](../evidence/sdf-cached-commit-2026-10/README.md) retains
reproduction instructions, complete receipts and report checks.
[Interactive HTML report](http://starship:8787/reports/3659235195747d263ebf8734145db49ba6c1bed67c875e6f62f63274e68d300f/).

## Decision

Hard-edit sample reuse passes the bounded cache-identity and usefulness gates.
It substantially reduces construction time without changing the fresh Cached
result in the qualified cases. Keep it as a private experiment: synchronous
commits still miss a 16.67 ms budget in every measured reuse trial.

The next study should investigate conservative local work, stable atlas slots
and bounded maintenance submissions alongside rendering. Global copying now
dominates Robot; canonical new-brick/numerical fallback sampling remains costly
for Guardian. Persistent local storage is justified as the next investigation,
with explicit correctness and scheduling questions still to settle.

Exact regional pruning fails this study's stricter raw-cache identity gate and
is excluded. Do not combine it with the qualified reuse route or widen numerical
tolerances. Its earlier point-level qualification retains its separate contract.

## What changed

`CachedCommitStudy` is a private helper linked into study executables. The
original `Field` implementation is unchanged apart from friend access declared
in its header. All 206 original SPIR-V files, 940 protected source/content files
and the public `sdf` executable retain their intake hashes. Cached defaults,
all public modes, canonical scenes, source/history/material formats and parser
limits are retained. No public editing UI or updater is added.

The candidate uses original full canonical hierarchy classification. For each
destination logical brick, it walks the old hierarchy through its ancestors.
Where matching old samples exist, it applies one admitted hard root tool at the
same lattice positions and writes the destination atlas. New bricks and
zero/subnormal/nonfinite ambiguous results use complete canonical source.
Union clips the tool to the workspace; subtraction uses the raw cutter.

Group-local compaction produces bounded new-brick and fallback-texel lists.
The canonical pass is separate from copying/tool work. A full list triggers a
full canonical rebuild. The old field remains immutable until a completed
candidate can replace it; the preview remains pending until a replacement
frame completes. Canonical CPU source/history is retained separately.

## Complete commit timing

RTX 5070 Ti / 610.57.04; unchanged depth-eight 8³ R16F bricks. Three interleaved
repeats, three warmups and ten measured commits per run: 48 runs, 480 measured
commits and 144 warmups. Each table cell summarizes 30 commits at the current
visible radius-.065 add/cut placement. Full and reuse reset to the same source
for every trial. All samples, finite-run tails and stage measurements are saved.

| Subject / operation | Pooled full ms | Pooled reuse ms | Speedup | New-field full ms | New-field reuse ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Guardian add | 1062.149 | 116.038 | 9.15x | 1078.723 | 123.696 |
| Guardian cut | 1030.369 | 87.741 | 11.74x | 1046.124 | 96.195 |
| Robot add | 100.620 | 27.305 | 3.69x | 108.569 | 34.730 |
| Robot cut | 97.170 | 24.592 | 3.95x | 104.852 | 32.222 |

These are medians of complete commit-to-ready wall time: study validation,
allocation where selected, GPU construction and waits. Pooled means an already
allocated candidate Field. New-field means allocation/initialization each
commit, not process cold start. Initial parsing and pipeline setup are separate.
All useful speedup gates (5x Guardian / 2x Robot) pass, including new-field runs.
**Zero of 240 reuse commits meets 16.67 ms or 11.11 ms.**

No owned builds, analysis or encoders overlap timed native jobs. Rendering does
not run concurrently during timing; the host target is blank 16x16. This is
construction latency, not sustained editing throughput, render interference,
physical input-to-display latency or XR acceptance. Boundary GPU observations
are retained; continuous GPU exclusivity is not claimed.

Pooled stage medians explain the remaining cost:

| Stage ms | Guardian add | Guardian cut | Robot add | Robot cut |
| --- | ---: | ---: | ---: | ---: |
| Study validation | 14.175 | 14.972 | 1.368 | 1.423 |
| Full hierarchy classification | 13.321 | 12.663 | 1.164 | 1.139 |
| Old-sample copy and tool | 16.238 | 15.597 | 18.210 | 17.630 |
| Canonical fallback | 69.613 | 41.812 | 5.253 | 3.028 |

Stage medians need not sum to the total median. Validation includes repeated
serialization/admission in this private helper. Removing allocation alone saves
roughly 8 ms; it cannot remove the dominant work. All destination bricks are
still visited and rewritten.

## Identity, repetition and publication

61 pairs match all reachable logical topology, active logical leaf sets and raw
R16 bits: 35 fixture/radius cases, eight Guardian plus sixteen Robot chain steps,
and two forced-overflow full-rebuild controls. This covers **2,953,737,728 stored
sample comparisons**, with independent dump byte comparisons and saved-file
hash/size verification. Repeated chains remain within unchanged source limits;
this is not long-session capacity qualification.

The ordinary cached-profile geometry controls retain their earlier contract.
Matching the fresh cache transfers the sampled-field result; it does not
recertify analytical accuracy. The prior unscored Guardian depth pixel and
sub-grid/grazing preview-to-bake limitations remain explicitly unqualified.

14 native sequences cover 66 frames, normal commits and five failure/retry
dispositions per subject. A frontier-capacity limit produces a real exception
during GPU classification. Other serialized tests cover a pre-allocation
failure disposition, cancel before submit, stale branch identity and discard
after submit. Eight negative requests exercise admission/base identity.
These injections do not establish concurrent input or GPU preemption.

Failures preserve the exact old preview and one pending edit. Every retry
settles byte-identically to an independent render from the preserved original
Cached executable. All 14 retained source trees/histories match their inputs;
four retained files replay through that original executable with identical
images, including material serialization. Frame completion includes readback
synchronization and excludes PNG encoding.

Two Fields plus bounded lists request 1,835,432,420 bytes (1.71 GiB). The largest
diagnostic staging buffer requests 76,695,844 bytes (73.1 MiB); their conservative
sum is below 2 GiB. This screens requested geometry/readback resources, not
allocator residency, CPU scratch or the complete renderer working set.

## Why regional pruning is excluded

A4 passes all 23 small fixtures and six Robot radius cases. Each of six Guardian
cases has 172,382 differing R16 words, including two signed-zero differences,
with matching topology. A16 passes the small fixtures, but fails the four
current subject cases. Guardian leaf sets differ; Robot has 148,031 add and
148,441 cut sample differences.

The native scorer's legacy `r16_differences` field is a size sentinel when leaf
sets differ. That value is **not a count of differing matched samples**. The
report leaves those Guardian A16 counts unscored and retains the original
receipts for inspection.

A same-kernel diagnostic at selected failure points records small full/pruned
float differences and distinct raw R16 writes. One Guardian A4 example changes
0.00409698486328125 to 0.004096955060958862: raw words change 7218 to 7217 while
`packHalf2x16` yields 7218 for both. Raw storage is the cache oracle, not a
packed-half reconstruction. These selected points are diagnostic, not an
unbiased error distribution or a general claim about all format rounding.

Eight short rejected-route cost runs (24 measured commits, eight warmups) remain
descriptive only. They do not enter the accepted speedup matrix. No general
pruner port or tolerance relaxation is adopted.

## Next experiment boundary

Derive conservative dominance bounds for unchanged sampled bricks and newly
active cells under one hard root operation. Include finite precision, workspace
clipping and shared endpoints. A raw primitive AABB does not bound the entire
scalar field. A possible center/Lipschitz bound could eliminate whole-brick
canonical evaluation; that is a hypothesis requiring adversarial native tests.

Then compare stable slots or copy-on-write patches against global atlas
rewriting, including map/render overhead, bounded capacity, rollback and reader
retirement. Retain full canonical rebuild for arbitrary tree changes and
recovery. Finally measure bounded maintenance submissions alongside ordinary
Cached rendering, complete-frame deadline misses and one-edit service rate.

That plan is follow-up work. This loop implements no persistent allocator,
asynchronous update service, continuous input, brush/undo UI or XR path.
