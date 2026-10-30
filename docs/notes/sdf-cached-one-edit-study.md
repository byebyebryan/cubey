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

Native sequence: base -> preview -> commit -> settled. Also inject one failure
after candidate construction and before publication, verify unchanged old
display/pending state, then explicitly retry. The settled image must match a
fresh original-executable Cached render of the full edited canonical source.

Deliver locally committed reproducible evidence and a published HTML report.
No push, project-site deployment, allocator/updater framework, indefinite editing,
source-capacity increase or XR qualification is authorized by this bounded loop.

## Progress

Planning checkpoint. Runtime implementation and native results follow in later
sections; prior general-tracer measurements are historical comparison only.
