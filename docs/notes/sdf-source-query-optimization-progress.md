# SDF source-query optimization loop

Date: 2026-10-29. Contract: [execution plan](sdf-source-query-optimization-plan.md),
committed at `0737bec9`. Work remains local on `sdf-tracing-comparison`.

## Checkpoints

- Baseline frozen before implementation: binary plus all 116 existing SPIR-V
  files and 155 source/content fixtures in the selected protected directories.
  Raw receipts: `outputs/sdf/source-query-optimization-20261029/`.
- A candidate adds startup selection, ten opaque source-lit wrappers and
  requested/effective optimization receipts. General, current reference,
  sampled, optical and debug shaders remain available. Automatic still selects
  the reference pending qualification. No per-edit pipeline creation is added.
- A controlled GPU fixture calls the actual opaque `surfaceRadiance` function
  with forced trace exhaustion. It checks front/back/grazing/tiny-sign and
  nonfinite inputs, exhaustion propagation and geometric query origins.

- `09ca1d74`: A candidate checkpoint. Its 129-job main qualification passes,
  including ten frozen 155-check query routes, Guardian/Robot 1080p captures,
  analytic/hybrid history and 21-action per-frame eligibility/failure replays.
  Another 12 appearance jobs and 42 lighting/near-surface image jobs pass.
- The 14-case controlled GPU exhaustion/nonfinite/origin fixture passes. A real
  Xvfb source window survives two resizes/recreations, orbit, zoom, reset and
  material editing with no field build and exactly three pipelines each time.
- Rejected harness attempts are retained: missing resume metadata, then camera
  distances below the existing 0.6 admission limit. The completed edge suite
  uses an enclosing clipped sphere at admitted camera distances; no camera
  guard was weakened. The first documentation check needed an index link.

## A measured result and native completion

Three interleaved repetitions / 5,400 completed GPU timestamps compare current
automatic reference and explicit shadow selection on Guardian and Robot:

| Guardian view | Reference GPU p95 ms | Shadow GPU p95 ms |
| --- | ---: | ---: |
| Preset | 21.510–21.645 | 19.183–19.403 |
| Side | 22.914–22.942 | 21.694–21.839 |
| Rear | 20.867–21.002 | 16.964–17.286 |

Robot ranges overlap without a meaningful regression. No owned build/test/capture
overlapped these timings. The source window additionally proves normal-debug
switching, identical restored lit pixels, retained pixels after an invalid owned
scene reload, and successful retry. Cursor-free captures are required: early
window attempts recorded the X11 pointer, and arrow keys controlled orbit rather
than selecting the debug dropdown. The successful harness uses inspected rows.

These results permit A's bounded automatic selection; final-binary verification
and evidence closeout remain required. Interactive root-far and physical acceptance
remain outside the admitted scope.

## B numerical and certificate audit

The separate primitive probe checks 82,388 points/stencils: 51,792 on Guardian
cones and 30,596 synthetic samples including cap/body seams, containing-sphere
boundaries, zero/equal radii and the minimum legal half-length. Both the original
CPU evaluator and original GPU function remain independent references. No new
point or normal failures occur against the frozen 2e-6 point / 1 degree normal
contracts. The largest GPU field change is 5.96e-8; the maximum Guardian normal
difference is 0.184 degrees. The probe's aggregate table includes independent
synthetic nodes in addition to the 2,021-instruction Guardian program; its larger
allocation is not a newly admitted authored source size.

The cache computes the same single-precision slope, `std::sqrt(max(0,1-s*s))`
and height-times-radial expressions as the unchanged canonical CPU primitive.
It stores their rounded float values in a 16-byte companion entry. Degenerate
containing-sphere entries retain the original evaluator. GPU field evaluation
keeps the original transforms, point-dependent lengths/dots, cap/body tests,
scaling and CSG operations. Source indices, not compacted program offsets, index
the table. No authored instruction, source hash, normal stencil or tolerance is
changed.

The pruning guard bounds GPU evaluation against the canonical CPU field, not
against a previous GPU image. Replacing GPU-only shape-invariant arithmetic
with its canonical CPU float values removes those error sources from the
existing operation budget; it adds no new arithmetic stage to that comparison.
Point-dependent operations and operator paths retain the original budget.
Root bounds and far certificates still come from the original source/CPU
builder. No guard widening is used. This audit depends on retaining that exact
coefficient expression and float layout; algebraic rewrites would reopen it.
Vulkan allows differing division/square-root rounding and preserves buffer-load
bits, so this does not promise GPU bit identity across implementations.
[Primary precision rules](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html).

The independent frozen 155-check native query suite also passes with prepared
values active. Initial native captures change only 96 / 185 / 79 Guardian
1080p pixels (preset/side/rear), maximum one color level; Robot's ineligible
multiple-material path is exact. Native differences are retained, not approved
by refreshing a golden. Publication tests must compare retained generations
with fresh prepared states, while separately reporting original-GPU differences.

## B publication boundary

B stays explicit, static headless analytic/pruned A64 root-far atmosphere. Its
candidate snapshot prepares the source program, derived table, appearance
buffers and a compatible descriptor generation before activation. Queue-idle
retires old in-flight use; activation only moves prepared resources and published
metadata. Appearance-only changes retain the cone generation. Geometry, source
reordering, undo and redo replace it coherently. Faults cover preparation,
upload, appearance upload, descriptor preparation and the activation boundary.
No engine API or windowed root-far admission is changed.

## Independent B timing and combination admission

B-only's 36 interleaved runs / 5,400 GPU samples pass the proposed gain/regression
gate. Guardian GPU p95 ranges are 20.025–20.186 ms preset, 21.870–22.051 ms side,
and 19.450–19.508 ms rear. Robot remains on the unchanged multiple-material
shader, without a meaningful regression. Combination admission follows this
independent result; no gains are added arithmetically.

The combined candidate passes its own 155-check prepared-kernel oracle and
15-action publication replay against six fresh native states. Its Guardian
and Robot images are byte-identical to B-only. Six additional patterned/bumped
cone lighting configurations cover AO/ground/soft shadows, clay, disabled bump,
disabled procedural shading, grazing light and a raster occluder; combined and
B-only images match exactly there too.

Supplemental independent analytic fixtures add 21 scored checks per mode for
equal-radii capsule geometry, tangent depth brackets, clipping, inside exits,
rotation/scale, zero endpoint radii and adjacent-float containing boundaries.
All four modes pass with the original tolerances. Twelve real CLI negative
cases retain the study's headless/source/axis/motion/lighting boundaries.

Actual authored-history prefix replays (nine completed frames per mode) cover
empty/first/second, undo, redo, branching, appearance reuse and failed-prefix
retry. B's derived generations follow the resulting compiled geometry exactly.
Full Guardian replays in all four modes retain failed pixels and restore undo
pixels exactly. B and combined report 32,336-byte tables, a 64,672-byte peak for
two device generations, and 32,336 bytes of transient staging payload. These
figures exclude descriptor/driver allocations and do not claim total VRAM use.
One replay per mode describes geometry publication at roughly 139–144 ms;
this is separate from repeated steady GPU frame timing.

Final-binary automatic, B-only and combined qualifications pass. The same binary
passes twelve focused SDF/shader/documentation test groups. The final four-way
timing and evidence closeout remain pending; A is automatic only on its eligible
source route, while B and combination stay explicit studies.
