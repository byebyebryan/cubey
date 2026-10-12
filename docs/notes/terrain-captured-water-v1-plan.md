# Terrain captured water integration plan

Date: 2026-10-11

Status: the implementation loop reached the first gate and rejected direct
placement against the existing Terrain ground mesh. No new bake or runtime
integration has started. See the [alignment result](terrain-captured-water-alignment-v1.md)
before resuming the later gates.

## Decision and scope

Give the shared Terrain foundation an optional, solver-neutral captured water
asset. Terrain loads saved water depth and flow, then renders them with the
existing Fluid 2.5D water treatment. It does not run a hydraulic solver, launch a
Python worker, or require the optional SynxFlow CUDA environment.

Start with one existing Mountain patch and two discrete states:

- **Rain-fed**: developed streams and lakes while uniform rainfall continues.
- **Post-rain**: the same run after input stops and water has had time to drain.
  Call it **settled** only if the declared settling proxy passes; otherwise
  retain the more accurate **receding** label.

Depth, water levels, and coverage remain fixed within a selected state. Surface
normals, flow texture, foam, and flecks may animate using that state's saved
velocity. This is an animated baked appearance, not continued simulation.
Standing water should not inherit the rain-fed state's flow or foam.

Do not add terrain erosion, river generation, site hunting, groundwater,
evaporation, snowmelt, persistent headwater inputs, or a solver port. Do not
reshape terrain to make water fit. Multiple sites, automatic weather-driven
state selection, continuous transitions between states, and default promotion
are outside this first pass.

## Existing reference and concrete gaps

The approved Mountain recording is
`outputs/fluid/offscreen-perf-20261010-CwWtK3/soak-600s-offscreen/live-0/static-recording/recording.json`.
It contains a dry initial frame and a developed frame at 38152.7734375 physical
seconds, about 10.6 hours. It is not a rain-fed/post-rain pair.

Use its existing unmodified Terrain Diffusion source,
`cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json`,
and 1024 by 1024 cells at 16 m. The patch is 16.384 km square, centered at source
world XZ `(11520, 19200)` m. Its first cell center is `(3336, 11016)` m; rows
advance in positive world Z and columns in positive world X. The full source is
2048 by 2048 at 30 m spacing; the 16 m solver grid is an interpolation, not new
terrain detail.

The existing recording reader already validates hashes and hydraulic fields,
and decodes velocity from momentum. It also supports piecewise-linear rainfall
history. However, the two-state exporter in
`projects/fluid/fluid_25d/run_live_perf_terrain_v1.py` deliberately assumes
constant rainfall. Do not bypass that guard or describe rain-off captures as
constant-rain recordings.

`Fluid25DScenic` is currently project-local and owns a complete scene graph.
It cannot be dropped into Terrain without also drawing a second terrain/sky.
Extract its water stages rather than copying the entire demo runtime.

Terrain uses radial/sector ground meshes, unlike Fluid's uniform B-spline
surface. Correct source coordinates alone do not guarantee that shallow water
fits the displayed ground. The current numerical bed differs from the sampled
source bed by at most 0.0050048828125 m; that verified serialization difference
does not measure the potentially larger ground-mesh reconstruction error.

## Implementation sequence and gates

### 1 Prove placement with the existing captured state

Before spending time on another bake, use the approved rain-fed frame to test
the proposed field view and coordinate contract. Bind its exact source and
focus, and use one matched view wholly inside the captured footprint.
Keep this probe limited to the existing recording loader, Terrain product
geometry, and a headless depth/coverage comparison. It is not a second permanent
water renderer; full renderer extraction remains step 4.

Verify cell-center convention, row direction, source identity, numerical bed
reference, vertical transform, and source-to-render translation. Water height
is reconstructed from the saved numerical bed and depth. Do not replace the
bed with a consumer-specific ground sample or add an arbitrary water lift.
Any supported scene placement transform must be applied consistently to ground
and water, not interpreted as a new hydraulic state.

Measure displayed ground height against the reference reconstruction at wet
locations. Report signed error, percentiles, maximum error, and affected water
coverage, including the existing stream and lake views. Add flat-lake and steep
bank controls so a plausible distant screenshot cannot conceal placement
errors. Define the visibility/error gate from the retained Fluid control before
tuning an alignment fix, not after viewing the candidate.

Do not draw water over Terrain's intentional center cutout or outside resident
ground coverage. The water patch is smaller than Terrain's default backdrop;
show its footprint and do not interpret uncaptured surroundings as dry terrain.

Gate: the mapping is correct and the matched rendered surface has no new
systematic floating, buried streams, tilted lake levels, or exposed square
patch border in the review framing. If this needs structural ground-renderer
changes, stop with the measured mismatch and propose a bounded alignment fix.
Do not silently turn this pass into a terrain rewrite.

### 2 Bake a genuine two-state pair

Use a fresh owned case and output directory. Keep the source, grid, Manning
coefficient 0.05, fall/open perimeter, and absence of authored drains unchanged.
Reuse the current 512 mm/h visual-demo forcing, not a claim of ordinary climate
rainfall. Start dry; do not add snapshot warm-start work to this pass.

Declare one schedule before running: 512 mm/h through 38160 physical seconds,
a 60-second taper to zero, then zero input. Capture the final full-rate state
and continue the solver through the post-rain period. Turning rain off is not
pausing the solver. Record actual native snapshot times and serialized forcing
history, including its float conversion and cumulative scheduled rainfall.

Use a purpose-built export path with tests for nonconstant forcing and exact
snapshot provenance. Reuse the existing native schedule/recording validation;
do not weaken the constant-rain isolation exporter. If the scheduled path cannot
produce a truthful history, stop rather than substitute undocumented controls.

Check recession every 30 physical minutes after input reaches zero. The
proposed visual settling proxy is three consecutive intervals with volume
change at most 1%, wet-area change at most 1%, and wet-union RMS depth change at
most 0.05 m. For these diagnostics only, define wet as depth at least 0.05 m and
use the previous interval's value as the relative denominator; handle empty
water explicitly. These thresholds do not change rendering coverage and do
not establish hydrological equilibrium.

Bound recession to 12 physical hours and the solver run to 45 wall minutes.
Save the latest valid post-rain state as receding if the proxy has not passed;
do not loosen thresholds retrospectively. If the wet phase cannot complete
within the wall cap, report the incomplete bake rather than claim a pair.
Retain the interval diagnostics and raw capture receipts, not a large permanent
replay timeline. Validate finite/nonnegative fields, dry-cell momentum, hashes,
and any available native numerical-health diagnostics. Closed basins may retain
water; an all-dry result is not required.

Gate: both selected states come from one continuous, unchanged-terrain run,
with correct forcing and honest state labels.

### 3 Establish the shared asset contract

Implement a small strict loader under `cubey::asset`, with solver-neutral field
semantics under `cubey::terrain`. Do not expose Fluid scenarios, live-session
commands, optional native environment paths, or CUDA types in public headers.

The versioned manifest binds source elevation identity, source-grid axes,
cell-center origin/spacing/dimensions, units, immutable numerical reference bed,
state payload hashes, physical capture times, state labels, and forcing/bake
provenance. Use relative payload paths. Generated bundles belong under ignored
`cache/`; raw bake receipts and validation evidence belong under `outputs/`,
following the existing terrain source storage boundary.

Each state supplies depth in metres and velocity XZ in metres per second.
Reuse the existing recording decoder's momentum-to-velocity calculation and
dry-cell handling; retain original depth/momentum captures as provenance.
Reference bed and depth must survive export bit-exactly. Do not add river
graphs, shoreline-distance fields, classifications, or new flow solvers.

A 1024-square reference bed is 4 MiB and depth plus two velocity components is
12 MiB per state: 28 MiB of raw runtime fields for the pair, before metadata,
GPU-derived resources, staging, and frame-slot copies. Measure actual RAM/VRAM;
do not present raw payload size as the runtime memory budget.

Gate: roundtrip/parity tests pass; malformed sizes, hashes, paths, nonfinite or
negative fields, and wrong source/placement are rejected. Missing optional water
leaves normal dry Terrain available. Invalid requested water is reported, never
silently repositioned or substituted.

### 4 Reuse the water renderer and integrate Terrain

Extract the existing water geometry/reconstruction, optics, roughness,
agitation, and flow/whitewater treatments into `cubey::render`. Adapt Fluid to
the same shared field input first. Keep its retained B-spline treatment and
existing configurable appearance; this is not another water-look experiment.

The caller supplies opaque HDR color/depth, camera, placement, lighting,
environment, and shadow bindings. The water component must not own a second
terrain, sky, atmosphere service, or hydraulic simulation. Render-only field
filtering and animation compute are allowed and must be measured separately
from hydraulic dispatches.

Insert Terrain water after opaque terrain/foreground and before clouds/HDR
post. Declare separate sampled/composed color resources where needed; never
sample and write the same image through an undeclared feedback loop. Provide
the appropriate water-composed depth to later cloud/occlusion stages.

Use a visual animation clock independent of the frozen capture time; current
held recordings freeze their visual clock. Allow animation to be frozen for
comparison. Reset flow/foam history atomically when the selected state, source,
or GPU resources change. Use each state's own velocities, with quiet standing
water. Do not carry over rain-fed foam history into a drained state.

Terrain gets a small opt-in interface: Water Off / Rain-fed / Post-rain, the
honest settled/receding status, and Animate surface. Source changes must either
load their matching companion or clearly disable water. Changing weather or
animation does not regenerate fields. No rain visualization, quiver, dye,
source/drain widgets, or live-worker controls are required in Terrain.

Gate: Fluid remains visually and numerically consistent under matched controls;
Terrain loads and renders the asset without starting a solver. Source/state
switches, frame-slot retirement, resize, animation freeze, and water-off behavior
pass. The public/shared target has no project-fluid or optional CUDA dependency.

### 5 Review the result and cost before promotion

Produce a compact HTML report under `outputs/terrain/` and link it from the
existing reporting index. Show Mountain water-off, rain-fed, and post-rain in
matched overview, stream, and lake views. Add a short motion clip with unchanged
field hashes and advancing visual time, plus raw depth/velocity and placement
diagnostics. Label this as animated captured water, not live simulation.

Use genuine headless Vulkan throughout; no GUI, X11, or Xvfb. Run loader and
field contracts, existing Fluid immutability/regression tests, and Vulkan
validation. Record dependencies and process evidence proving that consumption
does not launch SynxFlow or its worker.

Measure paired Terrain-only/water-on trials at 1920 by 1080, with two repeats,
fixed cameras/environment, warmup excluded, and no capture/encode/readback in
timings. Proposed gates are whole-scene GPU p95 at most 8 ms, incremental water
GPU p95 cost at most 2 ms per matched trial, and CPU frame-service p95 at most
4 ms. Do not pool percentile samples or equate a capped loop with measured FPS.
Record load/upload latency, peak memory, and any render-only compute cost.

Gate: the water demonstrably enriches the existing terrain without new bank
artifacts or a performance miss. If a gate fails, keep evidence and narrow the
fix; do not expand sites or promote defaults. User visual acceptance remains
separate from automated/headless validation.

## Quick review and readiness result

Fresh evidence is in
`outputs/terrain/captured-water-preflight-20261011-uPMhrX/`.

- Eight selected CTests passed in 13.58 seconds, including headless GPU recording
  immutability, recording/converter/backend/worker-host contracts, terrain
  surface fields, climate source, and product adapters. The worker-host check
  is a fixture, not a new live CUDA run.
- The existing terrain-presentation report revalidated, including its pinned
  source/capture hashes and stored performance gates. The current app and shader
  identity matches its sealed runtime. This does not measure the proposed
  shared Terrain water path.
- The approved Mountain bed, source bed, and both frame payloads match hashes.
  The developed fields are finite/nonnegative, dry momentum is zero, and volume
  recomputes to 93940293.73335421 cubic metres.
- Code review confirmed the forcing-export guard, independent rain/pause
  controls, project-local renderer coupling, and ground-mesh alignment risk.

Verdict: enough existing data and tested machinery to begin the bounded
placement/contract proof. There is no captured post-rain companion or validated
shared Terrain rendering integration yet. This is readiness evidence, not
implementation acceptance.

The next authorized implementation pass should follow the gates above, keeping
capture/export, shared contract, renderer extraction, and Terrain composition
reviewable as separate checkpoints. Commit/push behavior must follow that
pass's explicit authorization. This planning pass makes no runtime changes and
does not commit or push.
