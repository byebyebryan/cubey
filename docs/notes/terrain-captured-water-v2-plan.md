# Terrain captured water paired surface plan

Date: 2026-10-11

Status: implemented and accepted as opt-in V1 on 2026-10-12, with the measured
incremental water-cost exception below. Manual visual review remains deferred.

## Implementation result

The existing rain-on state now has a strict solver-neutral export and a second
consumer in Terrain. Fluid and Terrain share the paired ground/water/shadow
shader package and narrow render stage. App-owned engine composition handles
the fixed sky and single HDR transform; the render stage itself owns neither.
Terrain's explicit Captured patch never submits radial ground/shadows or clouds.
Its original Backdrop remains separate and unchanged. No new solver was run.

Final headless validation: 10 CPU/GPU/regression tests passed; 1280 paired GPU
samples have at most 0.351 mm gap error against the 1 mm gate. Three extracted
Fluid views are byte-identical to the frozen baseline. Terrain differs by at
most one 8-bit value because the two apps use sRGB versus UNORM attachments.
Saved GPU fields remain exact through water off/on, held/resumed motion,
two retired slots, resize, and actual patch/backdrop switches. An incompatible
source rejects before GPU work; the Backdrop resident/control images match.

Final 1080p render-only repeats: whole-scene GPU p95 3.11–3.78 ms, CPU service
p95 0.19–0.32 ms. The last lake repeat adds 2.034 ms over paired ground, narrowly
missing the proposed 2.000 ms water gate. **The automated strict gate remains
failed; the user explicitly accepted that measured limitation for opt-in V1.**
It is not a default promotion or manual GUI acceptance. Device-local heap usage
is 374 MiB for the whole app, versus 28 MiB of retained input fields.

Evidence and report:
`outputs/terrain/captured-water-paired-20261011-JK0Nmf/index.html`, served at
`http://starship:8801/terrain/captured-water-paired-20261011-JK0Nmf/`.
Raw commands, hashes, test receipts and all final performance repeats are in
that report's `summary.json`. Wider-terrain stitching, cloud depth composition,
rain-off and more sites remain deferred.

## Decision

Promote the existing working Fluid ground-and-water pair into a small shared
Terrain surface-patch component. Both surfaces must use the same numerical bed,
positive B-spline basis, uniform 2x topology, sample coordinates, and placement.
This solves the identified reconstruction mismatch without changing terrain
data or hydraulic fields.

Use one existing rain-fed Mountain capture. Do not run another simulation or
implement the rain-off state in this pass. Saved water geometry and coverage
stay fixed; surface flow, roughness, foam, and flecks may animate independently.

The recommended first Terrain integration is an explicit **Captured patch**
view, showing that paired surface without the original radial ground underneath.
Keep the normal **Backdrop** view unchanged. Stitching the compatible patch into
the larger radial backdrop is deferred: overlapping ground, geometry seams,
shadow seams, and different surface filtering are a separate problem.

This narrows the first deliverable compared with the earlier local-patch
proposal. It establishes a reusable foundation component and a real second
consumer, but does not claim seamless water coverage over Terrain's full source
or its normal 16.384 km-radius backdrop.

## Reference and invariant

The existing recording is
`outputs/fluid/offscreen-perf-20261010-CwWtK3/soak-600s-offscreen/live-0/static-recording/recording.json`.
Select its developed frame at 38152.7734375 physical seconds, while uniform
rainfall remains approximately 512 mm/h. This is visual-demo forcing, not a
claim about ordinary climate rainfall.

Retain its 1024-square, 16 m cell-center grid, numerical bed, momentum-derived
velocity, source elevation identity, and source focus `(11520, 19200)` m.
The nominal captured footprint is 16.384 km square. The existing drawn grid
spans first-to-last cell centers; preserve that convention rather than extending
the surface by half a cell without a defined boundary rule.

At every displayed location:

```text
ground height = paired mesh reconstruction of saved bed
water height  = paired mesh reconstruction of saved bed plus saved depth
visible gap   = paired mesh reconstruction of saved depth
```

Triangle interiors must use the same diagonal and interpolation for both
surfaces. Matching only the analytic B-spline formula is insufficient if their
triangulations differ. Coordinate rebasing or translation applies to both;
do not alter bed/depth payloads or redefine water level from the old radial mesh.

## Foundation ownership

| Layer | Responsibility |
| --- | --- |
| `cubey::asset` | Strict optional single-state water-capture manifest and payload loader |
| `cubey::terrain` | Solver-neutral grid placement, bounds, paired reconstruction and patch semantics |
| `cubey::render` | Shared ground, water, shadow and render-only appearance stages |
| `cubey::engine` | GPU resources, field upload, frame-slot lifetime and a surface-patch runtime |
| Fluid and Terrain apps | Adapters, cameras, environment policy, asset selection, UI and reports |

Names and file splits may follow existing conventions during implementation;
the dependency boundary is fixed. Shared headers/targets must not import Fluid
scenarios, `Fluid25DGpuResources`, SynxFlow commands, CUDA, or an optional Python
environment. Keep the old backdrop runtime intact for its existing consumers.
This is not a general terrain engine or a generic render-graph extension API.

## Checkpoints

### 1 Export one existing state and establish the field contract

Use the current strict recording reader in an explicit offline conversion tool.
Export a source-bound numerical bed, depth, and velocity XZ. Reuse the existing
momentum-to-velocity calculation and dry-cell handling. Preserve bed and depth
bit-exactly and retain raw depth/momentum capture hashes as provenance.

The manifest records encoding, units, axes, row order, cell-center origin,
spacing/dimensions, source elevation identity, capture time, rain-fed label,
forcing provenance, and payload hashes. Use relative payload paths and bounded
allocation. Reject corrupt/nonfinite/negative fields, unsafe paths, malformed
sizes, and mismatched source/placement. A missing optional bundle is not a
failure of normal dry Terrain.

Keep generated assets under ignored `cache/` and receipts under `outputs/`.
One bed plus depth and two velocity components occupies 16 MiB of raw 1k-square
fields; staging, derived maps, GPU padding, appearance resources and copies are
additional costs to measure. Do not copy all live solver allocations just to
satisfy the old renderer's descriptor layout.

Gate: source/coordinate checks, strict rejection tests, and field/velocity
roundtrip parity pass. No solver or environment is needed to consume the bundle.

### 2 Extract the existing paired reconstruction and renderer

Move or factor the retained shaders/helpers, rather than invent new surface
math. Ground, water, and ground shadow geometry must share the same reconstruction
and 2x mesh. Preserve source-aligned material masks, corrected whole-scene shadow
fit, accepted water optics/agitation and subtle whitewater. Bank noise stays an
option; do not introduce another appearance promotion in this loop.

Provide a narrow read-only field input and caller-owned camera, HDR color/depth,
lighting, environment and material bindings. The shared component must not
render its own sky, launch a worker, advance hydraulic fields, or apply a second
display transform. Neutral inputs for absent dye/cues remain internal render
details, not new requirements of a Terrain water asset.

Adapt Fluid's native Scenic path to this component first. Retain live streaming
and recorded-state adapters; unrelated solver/diagnostic/legacy paths remain
supported. Avoid copying a second permanent paired renderer into Terrain.

Gate: at mesh vertices and triangle interiors, GPU/CPU ground-to-water separation
agrees with reconstructed depth within 1 mm. Include flat lakes over sloped beds,
steep banks, dry corners, grid boundaries, translated placement and the fixed
triangle diagonal. Matched Fluid views must retain geometry, field hashes and
appearance with lighting/camera/clock locked. Record any justified pixel-rounding
delta instead of accepting an unexplained new look.

### 3 Prove the captured patch in Terrain

Add an opt-in captured-patch surface product to the existing Terrain app. Load
the matching source and capture, use the same source-aligned foundation surface
fields, and draw the shared ground/water pair into the app's scene graph.
Do not submit radial backdrop ground or its shadow casters in this mode.
The source is unchanged; its displayed reconstruction differs from the normal
Backdrop view and must be shown as such in the report.

Validate the captured rectangle against the source bounds and use its bound
focus directly. Do not require this optional mode to admit the normal full-size
backdrop disk, run automatic placement selection, or clamp/move the capture to
a different site. Patch cameras use their own supported framing rather than
pretending the existing backdrop camera envelope is interchangeable.

Provide Overview, Stream and Lake framing, Water visible, and Animate surface.
Water-off comparisons keep the paired ground geometry fixed; document the wet
material policy rather than silently switching ground products. Capture time
is informational, not an advancing simulation clock. Reset render-only flow
and foam history on field/resource changes, and do not upload static fields
again every frame. Apply source/product changes atomically and retire old GPU
resources through existing submission tickets.

The app names the mode **Animated rain-fed capture**, not live simulation.
Show source, capture age, and covered footprint. Views must stay inside supported
coverage or clearly reveal its finite boundary; do not present surrounding
uncaptured terrain as hydraulically dry. Source mismatch disables the requested
capture with an explicit error and leaves a valid normal backdrop path available.

Gate: real Terrain headless images show intact streams, grounded water and flat
lake surfaces. Off/on, freeze/resume, resize, source mismatch and returning to
Backdrop work without stale descriptors or residual patch shadow/depth.

### 4 Validate composition and cost before expanding

Start parity and performance checks with clouds disabled. The current Scenic
water pass samples opaque depth but does not write composed water depth; it is
not automatically safe for Terrain's later depth-aware cloud composition.
V1 captured-patch mode must explicitly report clouds as unsupported/disabled
unless a bounded water-depth composition pass is implemented and validated.
Do not silently feed ground-only depth into clouds and claim complete composition.

Opaque ground/foreground precedes water; the app retains atmosphere/HDR ownership.
Keep refraction's original opaque color/depth separate from outputs. If clouds
are supported, resolve a documented visible-water depth policy for their input
and test foreground occlusion, shallow edges and lakes. A broader transparency
or cloud-renderer rewrite is outside this pass.

Use genuine headless Vulkan, without GUI, X11 or Xvfb. Render matched old Fluid,
shared Fluid, paired Terrain ground-only, and paired Terrain water-on views at
1280 by 720 for visual review. The old Terrain backdrop is an additional dry
geometry control, not the baseline for isolating water cost.

Profile the paired ground-only/water-on cases at 1920 by 1080, two repeats each,
fixed environment/cameras, warmup excluded, and no capture/encode/readback in
timings. Proposed gates: scene GPU p95 at most 8 ms, water-on minus matched
ground-only GPU p95 at most 2 ms, CPU frame-service p95 at most 4 ms. Profile
Overview, Stream and Lake separately. Measure load/upload latency, CPU/RAM/VRAM,
render-only flow/foam costs, and confirm zero hydraulic dispatches. Do not
equate that zero count with an absence of all GPU compute.

Publish a compact HTML report in the existing reporting index. Include dry
geometry controls, matched streams/lakes, a short animated/frozen clip, unchanged
field hashes, timings, and explicit scope limits. Automated/headless validation
does not replace user visual acceptance; no default promotion is implied.

Gate: the shared paired component works in both consumers without numerical
changes, unexplained visual regressions, or performance misses. Commit validated
checkpoints only when the implementation loop authorizes it; do not push by
default.

## Deferred continuation

After the paired patch is accepted, plan its boundary handoff to the larger
backdrop separately. It must replace overlapping radial ground, not draw on top
with a depth bias. Keep any transition outside the water review region and
inspect it in color, depth and shadow across camera/light directions. Masking
color alone while retaining incompatible depth or shadow casters is not enough.

If wet streams reach the chosen boundary, the source does not supply a dry seam;
do not dry them artificially or claim a seamless wider capture. Keep the finite
patch view or propose the smallest necessary coverage expansion later.
Rain-off drainage, additional sites, snowmelt/baseflow, erosion, source surveys,
global terrain replacement and new hydraulic math remain deferred.

## Original planning review

Current code inspection confirms the working paired shaders and strict recording
reader exist, while Scenic still couples them to project-local resources and
its complete sky/scene graph. Terrain supplies a usable HDR/depth composition
point, but its current product, shadows, placement envelope and cameras are
backdrop-specific. Those are reasons to add an explicit paired patch mode,
not assume a water-only drop-in or redefine the backdrop contract.

The earlier CPU rejection remains valid. This plan addresses its cause by
sharing reconstruction; it does not claim the new GPU component, visual parity,
cloud depth handoff or performance gates are already validated. First implementation
acceptance is a matched rain-fed patch, not seamless full-terrain integration.

## Code anchors

- `projects/fluid/sim/fluid_25d/fluid_25d_recording.cpp`: existing strict field
  reader and velocity decoding for offline conversion.
- `shaders/cubey/terrain/surface_patch/fluid_25d_bspline_surface.glsl`: shared
  positive basis, mesh interpolation and fixed triangle diagonal.
- `include/cubey/render/terrain_surface_patch.h`: narrow paired render stage.
- `src/cubey/engine/terrain_surface_patch_scene.cpp`: app-owned paired scene
  composition, visual-flow filter and caller environment support.
- `src/cubey/engine/terrain_water_capture_runtime.cpp`: strict capture binding,
  retained GPU inputs, render-only presentation and immutable readback.
- `projects/fluid/sim/fluid_25d/fluid_25d_scenic.cpp`: thin Fluid adapter.
- `include/cubey/terrain/terrain_surface_field.h`: original-source material
  field semantics, independent of a consumer crop and not live rainfall.
- `projects/terrain/terrain_app.cpp`: existing opaque, cloud and HDR composition
  point and product/resource lifecycle policy.
