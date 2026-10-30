# SDF quality/speed: a useful preview compromise

Date: 2026-10-29. Plan: `42ae900d`. Implementation: `1b4faefb`.
[Research plan](sdf-quality-speed-research-plan.md) ·
[execution record](sdf-quality-speed-progress.md) ·
[measured evidence](../evidence/sdf-quality-speed-loop-2026-10/README.md).

[Interactive HTML report: Where to spend analytical quality](http://starship:8787/reports/932bc8269b0a1bfc66894eaa883396cab1397e84142f1aa2199e529de8c60503/)
includes native comparisons, enlarged crops, motion clips and fixture controls.
The [delivery receipt](../evidence/sdf-quality-speed-loop-report-2026-10.json)
separates verified HTTP bytes from unavailable browser-render acceptance.

## Decision

Retain two explicit private headless candidates: C refines a cached primary
hit, and C+N additionally evaluates source CSG normals at that refined hit.
Both meet the proposed 16.7 ms GPU p95 screen in all ten measured views.
C is inexpensive; C+N provides much better normals. Neither qualifies as
detail preserving: missing thin geometry, cavity first-hit errors and close
Robot silhouette failures remain. Sampled/full remains the default.

This is a measured preview option, not automatic quality switching, a windowed
root-far port, a new editor mode or human visual approval. Nothing was pushed
or deployed to the project site.

## Matched native frame cost

RTX 5070 Ti, NVIDIA 610.57.04; 1920×1080 SS1, frozen atmosphere lighting,
shadows enabled, no AO/ground/clouds/occluder. Each range is three serial,
interleaved runs of 180 frames, 30 warmup and 150 verified samples per pass.
All 174 final runs use native binary SHA-256
`1c71ec2622022f2f56cd85a9e19b80b7290bc35c5b9408ac193fd1cd55e1f587`.

| Subject / camera | S cached | C refined | C+N | A analytical |
| --- | ---: | ---: | ---: | ---: |
| Guardian preset | 1.888–1.924 | 2.190–2.202 | 4.658–4.733 | 18.129–18.303 |
| Guardian side | 2.207–2.235 | 2.400–2.416 | 4.020–4.060 | 20.367–20.482 |
| Guardian rear | 2.121–2.126 | 2.358–2.386 | 3.705–3.717 | 16.233–16.377 |
| Guardian close | 2.663–2.668 | 3.132–3.162 | 8.885–8.923 | 33.373–33.490 |
| Guardian distant | 1.604–1.623 | 1.780–1.782 | 3.088–3.124 | 14.599–14.654 |
| Robot preset | 4.233–4.252 | 4.440–4.476 | 4.325–4.336 | 5.102–5.121 |
| Robot side | 3.629–3.644 | 3.834–3.843 | 3.779–3.813 | 3.422–3.430 |
| Robot rear | 3.592–3.615 | 3.699–3.729 | 3.595–3.634 | 3.618–3.636 |
| Robot close | 9.168–9.188 | 9.254–9.270 | 7.642–7.678 | 8.821–8.845 |
| Robot distant | 2.085–2.113 | 2.194–2.211 | 2.322–2.346 | 2.653–2.665 |

Values are GPU p95 milliseconds. All 348 final frame-timing boundary
observations report zero encoder utilization. This is an uncontrolled shared
host, without a continuous exclusivity measurement or interactive FPS claim.
No owned tests/builds/captures/encoding overlap this timing batch. Earlier
endpoint and publication batches observed shared encoder load up to 50%.

The adaptive analytical control remains expensive on Guardian: preset
17.679–17.738, side 19.108–19.278 and rear 14.991–15.128 ms. Robot side already
favors the ordinary analytical route. N and its zero-selection control were
also repeated across six core views using the same N SPV; their complete
costs, not a forecast query count, are retained in the evidence.

## What buys the quality

C starts from the original cached hit and refines the same quantized
trilinear reconstruction. Eight corner values give a local gradient bound;
advancement stops at cell boundaries, includes numerical progress guards and
uses a tighter 0.03125-voxel hit band. The optional work budget is 64 iterations.
Finite unresolved crossings or budget exhaustion retain the original cached
approximation. Nonfinite work is diagnosed. Cached secondary rays and their
offsets retain their original rule. The 0.05-voxel pilot was less accurate for
similar cost and was not shortlisted.

N evaluates six scalar source CSG queries for a finite-difference normal,
using the existing A64 root-far region cache. Every point uses its own region.
It adds no source primary or shadow tracing. N alone still uses early cached
hit positions; C+N fixes those positions before evaluating the source normal.

At Guardian preset, cached depth-error p95 falls from 0.300 to 0.015
pixel-footprint units with C. These are ray-depth errors divided by the source
pixel footprint, not lateral screen displacement. Compatible normal-error p95
falls from 1.274° with S, to 0.840° with C, 0.929° with N, and 0.064° with C+N.
Robot preset falls from 1.175° to 0.039° with C+N. All ten views and the tails
remain in `quality.json`.

Normal statistics include only paired hits within two voxel spacings; the
excluded incompatible counts are reported. A few wrong-front-surface pairs
have very large depth errors, so p95 alone is insufficient. The report retains
maximum depth, false foreground/background and native crops.

## The geometry ceiling is real

All cached policies still miss five source hits on the subgrid-sphere fixture.
Across 12 selected fixture scenes / 43 rays, C reduces matched-hit failures of
the frozen source depth contract from 27 to two. The remaining subgrid cavity
selects the later wall, about 0.1995 world units away; the tangent-sphere error
is about 0.00664 world units. C+N cannot correct a normal evaluated on the wrong
surface. Tightening reconstruction can also reject an earlier approximate hit
or make a tiny source feature look worse despite tracing the cache better.

The 64×64 orthographic query ROIs cover nominal 0.5/1/2/4-pixel widths and two
pixel phases. Independent mathematical sphere/oriented-box coverage uses 16
samples per pixel. Cached absence persists even when the omitted source
feature projects to at least two pixels. These are query/coverage diagnostics,
distinct from native shaded captures.

The 384 native pose frames cover two subjects, an arc and a close dolly, and
four policies. Every pose retains field and region generations. Boundary p95
is one pixel in all sequences, but Robot dolly maximum error reaches 10.05
pixels for every cached policy. Static Robot close reaches five pixels. Paired
RGB error improves with C+N, but unreprojected mask toggles are not an automatic
flicker score. Human motion/visual acceptance remains open.

## Editing and resources

Three full-scene repetitions apply a real primitive edit, undo, redo, failed
publication, retry and appearance-only edit. Request-to-visible-frame waits
for completed GPU work. Ordinary edit samples are:

| Subject | S | C | C+N | Analytical |
| --- | --- | --- | --- | --- |
| Guardian | 1128 / 1149 / 1151 ms | 1127 / 1143 / 1145 ms | 1266 / 1262 / 1059 ms | 163 / 160 / 161 ms |
| Robot | 98 / 98 / 98 ms | 98 / 98 / 99 ms | 118 / 120 / 122 ms | 27 / 27 / 27 ms |

Pooling the four geometry-changing actions across repetitions gives Guardian
S/C/C+N p95 of 1159.95 / 1156.69 / 1274.75 ms and Robot
98.02 / 98.65 / 120.93 ms. Neither candidate triggers the proposed stop rule
of both 25% and 10 ms regression. Robot C+N is close enough to that boundary
that broader workloads should reopen it. Existing cache generation is already
on the GPU; this does not solve full-field rebuilding or continuous editing.

Both subjects retain the existing 912,997,552-byte field allocation. C adds no
metadata. N/C+N add 6,443,520 region bytes on Guardian and 4,422,660 on Robot,
below 64 MiB per generation. Two simultaneous Guardian field+region requested
payloads total 1,838,882,144 bytes, below 2 GiB. Source-only rendering omits the
field allocation. Region CPU scratch peaks are about 8.20 / 4.81 MB; staging
and canonical program/material copies remain separate existing resources.
These are requested payloads, not total allocator residency or total VRAM.

Native full-HD geometry plus one raster-UV image uses 33,177,616 logical bytes,
below 32 MiB. Per-pose capture drains the GPU and reuses one graph-resource slot.
Multiframe profiling is rejected for that private capture mode.

## Qualification and provenance

- Native source contracts: 155 checks pass with unchanged tolerances.
- Controlled actual refinement helper: 22 GPU observations and 2,560 independent
  gradient checks pass. This is a quantized-cell contract, not global source
  geometry equivalence.
- Five routes pass nine history actions, including undo/redo image identity,
  appearance reuse, failed publication and failed camera rollback.
- Twelve focused CTest groups and ten actual private CLI admission rejections
  pass. Canonical builders/history/material sources and shared engine remain
  unchanged; all 166 original SPVs remain byte-identical.
- Diagnostic replay is checked against native fragment status/depth/normal.
  Isolated differences remain unscored with original four-epsilon / one-degree
  limits; silhouette measurements additionally exclude two-pixel neighborhoods.
  Final static quality uses the same final binary as timing.
- Earlier contracts/history/publication and compiler packets retain their own
  binary/source/tool identities. Subsequent changes fix diagnostic-only
  descriptor/layout/resource lifetime. They are supporting evidence, not
  relabeled final-binary observations.
- Failed CSV, descriptor/layout and RGB-mask inference attempts are retained
  and excluded. The hybrid ladder is a short context pilot, not final repeated
  timing. Implausible driver local-memory statistics are excluded.

## Next questions, not adopted changes

If preview quality is sufficient, a future user-facing mode needs visual
approval and windowed/runtime qualification. If thin geometry must survive,
investigate selective source visibility that also catches uncertain cached
misses. Same-sign cache samples cannot certify that no source feature exists.
The current C+N implementation provides no such certificate.

Edit latency needs a separate incremental-publication plan. Automatic
preview/settled switching needs transition and hysteresis tests. Neither is
implied by the static frame result or implemented here.
