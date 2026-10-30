# Cached plus one pending edit: bounded native study

Date: 2026-10-30. Follow-up to the general live-CSG experiment. The user selected
Cached as the default and requested a smaller test: zero or one pending
operation, committed before accepting the next operation. Retained canonical
source/history is separate from the pending render operation.

## Contract and scope before implementation

- Change authored/workshop `auto` to Cached. Keep all explicit rendering modes,
  preset sample settings, source/history/material formats and canonical scenes.
- Zero pending operations select the existing Cached renderer and shader,
  without a private control buffer, extra tracing checks or source regions.
- The private active preview supports one hard union/subtraction of a convex
  sphere or unrounded box, with compiled rigid/uniform transforms. Reject
  multiple leaves, smoothing, arbitrary domain changes, palette replacement,
  glass and changes to the retained base prefix before GPU allocation.
- Reuse existing Cached tracing for the base. A ray/convex-primitive interval
  determines whether the operation changes its first visible boundary. When
  the primitive forces the result solid (union) or empty (subtraction), skip
  base boundaries inside that interval; resume Cached tracing after its exit
  if needed. Base normals use the old cached stencil; new brush boundaries use
  the primitive/clip normal. Apply the same composition to shadow rays.
- This is a cached-quality preview. Pending primitives may retain details that
  a fresh cache loses; quantify that change. Do not apply the previous strict
  analytical gate as a requirement to preserve every sub-grid feature.
- Commit is serialized: finish the preview frame, build a fresh candidate field
  from retained canonical edited source, prepare publication, then render the
  candidate through ordinary Cached. Retain the old cache and pending operation
  on a failed candidate. Retire the pending operation only after the replacement
  frame completes. There is no concurrent input/updater or undo UI in this trial.

## Frozen measurements and acceptance

Native jobs are serial, with no owned build/test/capture/analysis/encoding
overlapping GPU timing. Preserve external load boundary observations without
claiming continuous GPU exclusivity. Capture the original executable, 202 SPIR-V
hashes and 567 protected source/content hashes in
`outputs/sdf/cached-one-edit-loop-20261030/baseline.json` before changes.

Zero-edit output must be byte identical to explicit Cached from the original
executable at matching samples/camera/lighting. Use three interleaved timing
repeats, 1080p SS1, 30 warmup + 150 measured frames, and preserve every sample,
p50/p95/p99/max and 11.11/16.67 ms deadline counts. The zero-edit comparison
target is within max(5%, 0.2 ms) of original Cached; report variance/failure
instead of relabeling prototype overhead as an inherent editing cost.

Measure base, one visible add/cut preview and freshly committed Cached for
Guardian and Robot preset/close views. Separate warm rendering from allocation,
canonical full-field construction, publication preparation and the first
completed replacement frame. Rendering a preview cannot hide a synchronous
long GPU dispatch; expose that stall explicitly.

Independent controls include new additions, through-cuts/cavities, starts inside
solid/tool, workspace clipping, transformed boxes, source-capacity rejection,
sub-grid loss, material ownership and shadows. Compare preview and fresh bake
with the unmodified Cached policy rather than widening analytical tolerances.
Direct composition of Cached occupied intervals and the convex tool supplies
the query expectation; source known answers remain separately labeled.
For regular closed-form sphere/box controls, freeze a half-voxel depth allowance
(`0.5/(256*7)`) and two-degree normal allowance away from seams. This is a
separate cached-quality test, not a modification of the previous source gate.
Tangencies and sub-grid preview/bake differences are explicitly diagnostic.

Native sequence: base -> preview -> commit -> settled. Also inject one failure
after candidate construction and before publication, verify unchanged old
display/pending state, then explicitly retry. The settled image must match a
fresh original-executable Cached render of the full edited canonical source.

Deliver locally committed reproducible evidence and a published HTML report.
No push, project-site deployment, allocator/updater framework, indefinite editing,
source-capacity increase or XR qualification is authorized by this bounded loop.

## Progress

Planning checkpoint: `70ea8c5b`. Implementation adds a separate one-edit target,
four private shader entrypoints and an optional renderer attachment. The public
default changes to Cached; the original 202 shader binaries and 567 protected
source/content files remain byte unchanged.

The regular query pilot passes 29 rays at the frozen cached-quality limits;
sub-grid and tangent diagnostics remain separately reported. The first pilot
caught a clipped-cutter error that left the old cache's hit band at the workspace
front visible. Hard subtraction now composes the clipped base with the raw
cutter, as required by max associativity. Tangent roundoff receives a bounded
float projection allowance without changing the regular acceptance limits.

The initial active shader had multiple call sites for the nested cached walk.
Its 1080p pilot cost about 5.4 ms on Guardian and 19 ms on Robot preset views.
Reusing one call site and only resuming after an actually hidden base boundary
retains the query results and yields short-run pilots around 1.9-2.0 ms and
4.0 ms respectively. These short pilots are excluded from final timing; no
register/spill attribution is claimed without a hardware breakdown.

Pilot native commit and failed-candidate/retry sequences preserve the preview,
retain canonical source/history, retire pending state after a completed new
Cached frame, and match independently rebuilt original-executable Cached images.
The final sequence starts with no pending edit, activates one preview, and keeps
that operation until replacement-frame completion.

Final image review caught a workload error: the initial Guardian cut had zero
visible tool hits and changed no pixels; Robot's cut barely affected its view.
Those placement/timing receipts remain in `final/` as excluded coverage pilots.
New cut centers are frozen from measured visible Cached ray hits, moved inward
by .01 along their normal. Before timing, every subject/view/add-or-cut must
change at least ten of 160x90 Cached control rays, by classification or more
than one voxel in depth. This adds a visibility coverage gate without changing
the geometry error allowances. The qualified dataset uses a separate raw root.

## Qualified results and decision

The narrowed rendering hypothesis passes in this bounded trial. Cached is the
public default for authored scenes and workshops, with all five explicit modes
available and existing preset sample settings preserved. One pending hard
sphere/box union or subtraction is a private study path. Zero pending edits use
the original Cached shader and descriptors, with byte-identical images against
the preserved original executable's explicit Cached mode.

Runtime source: `e5bc3628`; visible-workload harness: `1a498e7c`. The final raw
dataset is `outputs/sdf/cached-one-edit-loop-20261030/qualified-visible/`.
The [evidence packet](../evidence/sdf-cached-one-edit-2026-10/README.md) retains
all commands, inputs, hashes, failures and complete per-frame measurements.

Native 1080p SS1 GPU p95 milliseconds, median of three interleaved runs. Each
run contains 30 warmup plus 150 measured frames; each table cell has 450 measured
frames. Lights/materials/camera match, with hard shadows and no AO/ground/clouds.

| Subject / view | Original Cached | Zero pending | Add preview | Cut preview | Fresh add | Fresh cut |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Guardian preset | 2.03 | 2.04 | 2.02 | 2.04 | 2.14 | 2.03 |
| Guardian close | 2.76 | 2.77 | 2.68 | 2.86 | 3.02 | 2.78 |
| Robot preset | 4.32 | 4.34 | 4.03 | 4.07 | 4.26 | 4.30 |
| Robot close | 9.20 | 8.95 | 7.96 | 7.87 | 9.29 | 9.10 |

All four zero-edit timing comparisons pass the frozen max(5%, 0.2 ms)
allowance. Every measured warm rendering frame is below 11.11 and 16.67 ms in
this finite mono SS1 run. Those samples exclude commit stalls and establish no
continuous-input, stereo or physical VR acceptance. A brush changes geometry
and shading work, so lower preview times are not a general speedup claim.

The visible cut coverage is 64/244 changed Guardian rays at preset/close, and
68/229 changed Robot rays. Additions change 112/590 and 94/368 respectively.
Every changed-ray count uses 160x90 matched Cached control rays and exceeds the
pre-timing ten-ray visibility gate. No camera/shadow replay exhausts. The 29
regular independent controls pass; sub-grid and tangent diagnostics are separate.

## Commit remains the bottleneck

Eight serialized sequences cover both subjects/operations, ordinary success and
an injected failure after candidate construction before publication, followed by
explicit retry. All settled images match fresh original-executable Cached
renders byte for byte. Failed candidates retain the exact old preview image and
pending operation. Retained canonical source/history includes the new operation;
pending rendering state is cleared only after a replacement frame completes.

Across four successful attempts per subject, median ready-to-render commit time
is **1,119 ms Guardian** and **104 ms Robot**; median replacement-frame completion
is 1,125/111 ms. Field construction accounts for about 1,111/96 ms, with roughly
8 ms allocation. These 960x540 SS1 sequences use the same 3D cache resolution.
Replacement completion includes readback synchronization but excludes PNG
encoding. Renderer preparation is prewarmed and separately recorded.

Two fields request 1,825,995,104 bytes of geometry payload (about 1.70 GiB), before
renderer/staging costs; this is not a residency measurement. The old cache stays
alive until publication. No background builder or continuously accepted stream
is implemented, so the current commit pauses feedback. Retaining only one
pending render operation does not remove the canonical source capacity limit.

## Preview/bake differences remain explicit

At the 320x180 payload resolution, regular visible add/cut comparisons have no
scored foreground/background changes. Guardian retains one unqualified native
depth pixel at (137,121), present in zero-edit Cached, preview and fresh Cached;
Robot has no flagged pixels. Excluded pixels are not certified as passed.

Depth p99 for additions is about 0.000326 Guardian and 0.000255 Robot, but four
Guardian and five Robot matched-hit pixels change depth by more than two voxels.
The largest changes are 0.236/0.0368 world units: foreground brush versus body
layers on grazing rays. Independent ray/sphere clearance is positive but below
ordinary Cached's hit epsilon for the largest cases, supporting a hit-band
interpretation. This is not a strict preview/bake equivalence pass. The packet
retains those tail diagnostics rather than relying only on small p95 values.
Cut normal maxima at edges are about 9.4/22.4 degrees despite low p95 values.

The pending primitive is analytical, so the known radius-0.35h cavity still
appears during preview and disappears in a fresh sampled bake. This trial
accepts the selected cached-quality contract and records the change; consistent
preview tolerance/resolution or a minimum-feature policy is still a separate
decision. Earlier analytical-source tolerances are unchanged.

## Validation and next boundary

[Interactive results and native images](http://starship:8787/reports/7052366b02e5c143cbc8634222fd59a5fb67d3970dbf4a8cf15d2d1edd78775e/).
The reproducible packet is in
[`docs/evidence/sdf-cached-one-edit-2026-10/`](../evidence/sdf-cached-one-edit-2026-10/README.md).

The focused 13 SDF/documentation/shader checks pass; all affected mode/selection
test targets were rebuilt. Owned native Xvfb checks show Default -> Cached at
Robot's retained SS2 and the workshop's SS1, exercise all explicit modes and
sampling/resize controls, and check workshop material/specimen publication.
Original 202 SPIR-V files and 567 protected source/content files are byte
unchanged. Source/history formats, canonical scenes/presets and builders remain
unchanged. Commits are local; no push or project-site deployment.

Next, compare cheaper canonical cache construction against the full-field
control, including work-unit time, preparation, rendering interference, memory
and publication. The renderer now supports a useful small pending state for
that experiment. Continuous input, longer editing sessions/source capacity,
undo UI and XR remain separate gates.
