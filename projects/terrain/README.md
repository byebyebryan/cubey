# Terrain

Project boundary: [Terrain Water](../terrain_water/README.md) owns the featured
live external-solver experiment. This dedicated Terrain demo consumes shared
terrain and saved-water landscapes without a running solver. The builtin
shallow-water experiments remain in [Fluid 2.5D](../fluid/fluid_25d/README.md).

`projects/terrain` is Cubey's active fixed-focus far-field terrain backdrop and
review application. It consumes an external `cubey.terrain.heightfield.v1`
asset, selects a deterministic source placement, bakes one continuous cached
mesh, and can replace that placement at runtime while rendering with shared
atmosphere, clouds, and HDR composition.

This is deliberately not a general terrain engine. The normal backdrop does not
provide close terrain, traversal, streaming, hydrology, vegetation, deformation,
collision, or planet projection. An optional captured-water patch is a separate
bounded review view, not a water layer stitched into that backdrop.

## Animated rain-fed capture (opt-in)

The shared foundation can load a single `cubey.terrain.water-capture.v1` bundle
containing source-bound bed, depth and XZ velocity. The captured patch uses the
same positive B-spline basis, 2x triangles, shadows, material masks and water
appearance as Fluid's Scenic renderer. No solver, CUDA, Python environment or
hydraulic dispatch is needed to consume it. Source elevation/placement, payload
sizes, hashes, units and finite fields are checked before rendering.

The [glTF + Terrain highlight](../gltf_viewer/README.md#showcase-highlight) is an
accepted external consumer of this saved-water foundation, using the same
runtime and shared materials with independent foreground/background depth
ranges. It does not add water to the separate radial backdrop or require a live solver.

The current locally retained Mountain bundle can be reviewed with:

```sh
build/release/projects/terrain/terrain \
  --terrain-heightfield cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --terrain-water-capture cache/terrain/water/v1/mountain-rain-fed-1024-16m/water-capture.json
```

For remote capture, add `--headless --output outputs/terrain/captured-water.png`.
`--terrain-water-capture-camera overview|stream|lake` selects observation-only
framing for this Mountain state. The UI offers **Backdrop / Captured patch**,
**Water visible**, and **Animate surface (not hydrology)**. Hiding water retains
the same paired ground and captured-depth wet material; it does not dry the bed.
Surface shading and flecks animate independently of the fixed numerical fields.

The finite patch is never overlaid with radial ground or radial shadow casters.
Normal placement selection is deferred until Backdrop is actually requested.
The capture view retains Fluid's fixed shared daylight and single HDR display
transform by default. `--terrain-water-environment shared` opts into the same
render-only clear-sky day/night controller used by Fluid, with the existing atmosphere
CLI controls and a compact **Environment (render only)** panel. Clouds and cloud
shadows are excluded from this elevated saved-water view: the existing surface
cloud model is not a qualified aerial/cloud-top solution. Normal Backdrop still
supports its existing cloud controls; Terrain's global cloud options apply there,
not to saved water.
The environment clock is independent of saved-water surface animation; numerical
fields and geometry/LOD remain unchanged. Terrain's normal backdrop environment
path is unchanged. This does not promote a new default or claim seamless water
coverage. See the
[shared environment qualification](../fluid/fluid_25d/shared_environment_v1.md).

An invalid requested capture fails explicitly in headless mode. The GUI instead
disables it with an error and retains the normal source picker/backdrop path.
`--terrain-water-audit` performs a one-time immutable GPU-field readback at
shutdown, outside profiling. The bounded headless transition study uses
`--headless --headless-render-only --frames 8 --terrain-water-transition-smoke`.

The excluded offline exporter is built explicitly:

```sh
cmake --build build/release --target cubey_project_fluid_25d_export_water_capture
build/release/projects/fluid/fluid_25d/cubey_project_fluid_25d_export_water_capture \
  RECORDING.json HEIGHTFIELD.json NEW_OUTPUT_DIRECTORY
```

It selects the existing last rain-on frame, reuses checked momentum-to-velocity
decoding, preserves bed/depth bit-for-bit, and refuses an existing destination.
Generated bundles stay under ignored `cache/`; reports and headless evidence
stay under `outputs/`. Rain-off, new simulations/sites and boundary stitching
remain separate work.

The first paired-surface review is at
`http://starship:8801/terrain/captured-water-paired-20261011-JK0Nmf/`.
Final 1080p render-only measurements on Starship's RTX 5070 Ti: whole-scene GPU
p95 up to 3.78 ms and CPU service up to 0.32 ms. The final lake repeat adds
2.034 ms of water cost, narrowly over the proposed 2.000 ms gate. This measured
exception was accepted for opt-in V1; the original strict-gate failure is retained.
Manual visual acceptance and seamless backdrop stitching remain deferred.

The headless report helper runs capture/performance/report phases separately:
`python3 projects/terrain/review_captured_water_v1.py OUTPUT --phase PHASE`.
It freezes binary/shader/input identity across phases and never launches a solver.
`--accept-measured-water-cost` is report-only and records an explicitly authorized
exception without rewriting the automated gate. Its pure report checks run with
`python3 projects/terrain/review_captured_water_v1_tests.py`.

## Product Contract

### Wider rain-fed captured backdrop (opt-in study)

The 2026-10-12 wider-water study reuses the same unmodified Mountain source and
external solver. Its matching 957-square / 64 m pilot and 1913-square / 32 m
candidate cover 61.184 km square; dimensions fit the existing elevation/climate
bounds instead of forcing an out-of-bounds 2k / 32 m grid. Both start dry under
continuous 512 mm/h visual-demo rain and retain sparse snapshots through twenty
physical hours. This is not ordinary weather, equilibrium or a permanent-water
classification. The larger catchment, resolution and duration can change water
levels; the original 16 m capture is kept unchanged, not spliced into the result.

The locally retained wider candidate can be consumed without any solver:

```sh
build/release/projects/terrain/terrain \
  --terrain-heightfield cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --terrain-water-capture cache/terrain/water/v1/mountain-wide-rain-fed-1913-32m-study/water-capture.json \
  --terrain-water-capture-camera landscape --terrain-water-detail fixed \
  --terrain-water-focus-x 11520 --terrain-water-focus-z 19200 \
  --terrain-water-focus-height 1342.083496 --terrain-water-span 11457.6
```

The fixed topology is built/uploaded once: paired ground, water and ground shadow
share field coordinates and indices. A 16 m fine region transitions to 32 / 64 m
geometry; aggressive 128–1024 m exterior steps were rejected after they distorted
distant wet silhouettes. The source/state remains unchanged, and a denser mesh
does not invent extra simulated detail. Existing materials, flow and whitewater
are shared; bank noise and numerical/render defaults do not change.

`--terrain-water-detail uniform` retains the old uniform 2x path (default).
`uniform-control` retains that geometry with the same fixed-region 4096 shadow
fit as `fixed`, for a fair topology comparison. `--terrain-water-detail-span`
sets the build-time fine-region size. `--terrain-water-detail-focus-x/z` pins it
independently of camera translations. Explicit `--terrain-water-focus-x/z`,
`--terrain-water-focus-height`, `--terrain-water-span`, `--terrain-water-yaw`
and `--terrain-water-pitch` override preset framing without moving the asset.
These are bounded scene controls, not a camera-following LOD/streaming system.

Wider water removes the old inner hydraulic cutoff but cannot remove the finite
outer source edge. Several headings in the full original-site orbit expose that
edge; the full orbit envelope is not visually qualified. Far shadow receivers
and close-up shores outside the fine region also remain unqualified. No masking
pretends the outside world is dry, and no default promotion or manual GUI
acceptance is implied. The normal Backdrop is still a separate unchanged product;
this is a wider optional captured surface, not an overlapping radial water layer.

Report: `http://starship:8801/terrain/wider-water-20261012-a1/`.
Both twenty-hour bakes completed within their bounds: 4.08 minutes wall for
64 m and 28.10 minutes for 32 m. The fixed 32 m consumer submits 3,901,302
triangles per surface pass versus 29,245,952 uniformly at 2x. Eight 1080p
render-only comparisons (two each for Landscape, Stream, original Lake location
and a new large-basin stress view) pass the proposed scene GPU <=8 ms / CPU
service <=4 ms gates: GPU p95 at most 3.292 ms, CPU service at most 0.317 ms.
Matched uniform Landscape controls measure 8.560 ms; incremental water costs
are recorded separately, not equated to attribution or monitor FPS. Reported
device-local heap usage peaks at 653 MiB. Twelve CPU/GPU/regression checks and
seven portable helper tests pass; original Terrain/Fluid PNG references stay
exact. The solver is stopped during consumer timings; these are not wider-live
streaming or native GUI performance measurements.

`study_wider_water_v1.py freeze|prepare|run --out OUTPUT --candidate CANDIDATE`
is an explicit offline tool using the existing optional environment and pinned
extension. Its roots are fresh owned leaves and each solver run has a 30-minute
wall bound. `review_wider_water_v1.py capture|performance|basin|report OUTPUT`
never starts a solver or GUI, preserves frozen input/runtime hashes and uses
genuine offscreen Vulkan. Generated bundles remain ignored local cache assets.
Capture/performance phases use standard Python; basin/report analysis uses the
existing optional NumPy/Matplotlib environment, without importing or starting
the solver. The installed interpreter path is recorded in the study protocol.

### World-first prepared sites (opt-in)

The finer display atlas and shared material readability follow-up are documented
in [Shared terrain readability V1](../../docs/terrain-readability-v1.md), with
matched controls at `http://starship:8801/terrain/readability-20261012-a1/`.
The reviewed footprint-filtered material is now the default for prepared sites,
captured ground and the established radial backdrop. The explicit equivalent
selectors are `--terrain-readability refined` and
`--terrain-surface-detail readable-detail`. Previous treatments remain available
as `--terrain-readability current` and `--terrain-surface-detail filtered-detail`.
Fluid's shared Macro preset also defaults to the new ground, with its validated
source-world material origin. Older Fluid material profiles remain references.
Promotion checks: `http://starship:8801/terrain/readability-defaults-20261012-a1/`.
The atlas is display-only; wider terrain still does not imply wider water.

The world/site study separates broad Terrain Diffusion coverage from saved water.
It uses one pinned seed/settings/coordinate mapping, a 262.144 km coarse survey,
and 16.384 / 32.768 / 65.536 / 131.072 km requested site boxes. Site centers are
admitted only if the complete outer backdrop plus 1 km camera movement and
480 m sampling margins fits the world. The coarse conditioning map is a survey,
not final rendered terrain. This is a bounded fixed/orbit foundation, not a
free-flight or infinite-streaming system.

With the existing landscape assets/tooling installed, explicitly prepare:

```sh
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase freeze
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase spike
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase survey
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-0
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-1
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-2
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase validate
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase publish
```

Each optional producer process has a 900 s wall limit; no downloads or installs
are attempted. Complete validated site packages are reused without starting the
producer. Cache publication refuses to overwrite an existing world package.
All generated data stays outside Git; normal build/test does not generate it.
The initial validated package is under
`cache/terrain/worlds/v1/` (see the report's `published.json` for the exact leaf).

Inspect a package with `--terrain-site PATH/site.json`. This prepares a sampled
terrain-only surface in memory: zero water, no fabricated hydraulic recording.
It reuses shared surface shading, filtering, fixed detail, exterior rings and
core-focused shadows. The CLI owns its source selection and resets on startup;
there is no new live GUI site picker. Far-context shadows remain unqualified.

For existing captured water, retain its original matching heightfield and add
`--terrain-water-context --terrain-water-context-source PATH/heightfield.json`.
The wider source must match producer, seed, height transform, coordinate mapping,
and every native anchor sample. The original capture hash/rounded-bed check
remains unchanged; raw water fields are not replaced. Wider ground is not wider
water coverage.

The same prepared packages now feed an explicit Fluid live rainfall collection,
using shared site admission, source sampling and correlated terrain materials.
Fluid displays only its full-resolution simulation patch; exterior ground and
backdrop LOD remain Terrain's responsibility, not a wider Fluid view.
Its rain-on states export through the existing solver-neutral water contract
and load in Terrain without CUDA or a running solver. See
[Prepared-world Terrain Water](../fluid/fluid_25d/world_water_v1.md) for launch
commands, finite-water coverage and headless qualification. This does not add
water to the terrain-only `--terrain-site` view or promote a new default scene.

The low-frequency-only export was rejected: its p95 difference from filtered
native terrain was about 216 m. V1 instead generates native tiles for selected
backdrops, then filters them. Runtime currently retains that native raster and
its filtered mip chain; compact exported tier views are not yet sparse-resident
runtime data. The initial three backdrops each took about 99–101 s including
producer setup, with exact native tile/source/site overlaps on this machine.
Dependency/model installation and cross-hardware reproducibility are not included.

Review: `http://starship:8801/terrain/world-sites-20261012-a1/`.
All 17 selected CPU/GPU/regression tests passed; three legacy views and a
published-cache reload remained pixel-exact. Render-only 1080p GPU p95 was
0.69–0.73 ms for terrain-only sites and 3.50–3.56 ms for the expanded-ground
water scene (previous coverage: 3.48–3.52 ms); CPU service p95 was below 0.31 ms.
These are headless measurements, not native GUI/frame-delivery qualification.
Review commands use `review_terrain_world_v1.py OUTPUT --phase capture|performance|report`;
the report/maps phase needs the existing optional NumPy/Matplotlib environment.
No solver or native GUI is launched by the review tools. No default is promoted.

### Prepared-world wider saved water (opt-in)

#### Named prepared landscapes and Apply/reset

The shared asset layer now exposes a pinned landscape catalog and admission API.
Terrain consumes it through the committed `terrain_landscape_presets.json`:
four saved-water presets and three terrain-only choices. This does not promote
a new startup default; no-argument startup remains the canonical legacy product.

```sh
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-local
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-wide
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-interior-wide
rtk proxy build/release/projects/terrain/terrain --terrain-landscape valley-interior-wide
# Same prepared site, without loading a hydraulic capture:
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-dry
# Explicit proposed-default trial; never implicitly selected at startup yet:
rtk proxy build/release/projects/terrain/terrain --terrain-landscape auto
```

`mountain-local` uses 2048² / 32 m water over nominal 65.536 km, plus the full
131.072 km terrain backdrop. The three `*-wide` presets use 2048² / 64 m water
over nominal 131.072 km. Both are independent twelve-hour storm states, not
nested water LOD levels. The preset automatically supplies the bounded 4.5M
mesh budget unless explicitly overridden. Direct site/source/capture arguments
retain their existing behavior; they override `auto` and conflict with a named
preset. Explicit legacy terrain/environment controls (including zero and false
assignments) also bypass `auto`, rather than being ignored by the saved-landscape
renderer. A named landscape rejects incompatible legacy controls; use `legacy`
or the capture-specific controls instead. `--no-clouds` is compatible with a
prepared landscape. Configuration-file and CLI assignments retain precedence
for rendering options. `--terrain-landscape-cache-root` supports another
explicitly prepared cache without recompiling or mutating packages.

The native UI exposes Site / Water state / Selected / Showing / Apply landscape
above either view. Apply prepares and validates asynchronously, uploads through
the GPU owner, resets the camera, and defers old resident destruction until its
submitted work completes. Preparation failure keeps the current scene. Only one
replacement is allowed at a time; temporary old/new CPU and GPU overlap is a
measured cost. Availability is checked at startup or explicit Refresh, not by
hashing payloads each UI frame. Show water and Surface motion do not reload
assets. Local/Wider framing and Reset camera remain independent of asset selection.
Native GUI layout and manual acceptance are deferred; headless lifecycle controls
exercise the same Apply path, including extent rebuild and pending-load shutdown.
Startup and Apply use one preparation routine. Uniform detail does not allocate
fixed topology, and context-off does not allocate the terrain exterior. Explicit
capture detail/budget/focus/span/material/visibility/motion settings survive
Apply; unassigned camera defaults reset for the newly selected site. A sticky
camera focus outside the new capture fails during CPU preparation, retaining the
old scene. Explicit Apply chooses the prepared view and releases the previous
legacy source/environment ownership.

Normal build, test and startup never run Terrain Diffusion or SynxFlow. To verify
installed packages, or explicitly install the exact retained study bundles:

```sh
rtk proxy python3 -B projects/terrain/tools/prepare_terrain_landscapes.py
rtk proxy python3 -B projects/terrain/tools/prepare_terrain_landscapes.py \
  --from-terrain-study outputs/terrain/world-sites-20261012-a1 \
  --from-water-study outputs/terrain/wider-water-world-20261012-gqeluh
```

The installer validates payload hashes, reuses complete matching packages and
refuses to overwrite changed/incomplete packages. Use `--cache-root` for a fresh
destination and `--preset` for a bounded subset. The source study bundles must
already exist; they are not downloaded or committed. A new hydraulic bake may
produce different recording/provenance metadata and requires a reviewed catalog
revision rather than pretending to recreate the pinned asset.

Explicit missing/invalid selections fail. `auto` selects the proposed mountain
local preset only when available, otherwise missing/incomplete metadata or
payloads fall back to canonical terrain with a notice. Changed pins or invalid payloads are
errors, not silent fallback. The availability probe validates present metadata,
confined paths and payload sizes even when another file is missing; missing files
do not mask a changed pin, wrong-size payload, unsafe link or unreadable metadata.
All symlinks below the selected cache root are rejected, including dangling links.
Full payload hashes/numerical validation run when loading an available selection,
not on every availability query. Fixed saved fields and decorative motion are
labelled in the UI; there is no adjustable live rainfall in this consumer.

Review: <http://starship:8801/terrain/prepared-landscapes-20261013-zYn3Vs/>.
Final default promotion remains pending joint review. The live Fluid demo,
solver equations, existing material defaults and generated data are unchanged.

V1 qualification: 37 selected regression groups passed with no skips; four old
locked-camera wet frames and the fresh-cache mountain/local reload are pixel-exact.
All eight 1080p headless render-only repeats pass the 8 ms whole-scene GPU / 4 ms
CPU-service gates: GPU p95 3.578–4.161 ms and CPU p95 at most 0.228 ms, with no
solver, encoding or image readback during timing. Steady device-local heap
observations peak at 759.06 MiB. Wet Apply preparation is about 1.39–1.45 s plus
0.50–0.57 s GPU installation; the diagnostic replacement sequence peaks at
1292 MiB process RSS. The largest post-install GPU heap observation before old
resident retirement is 815.56 MiB, not a continuously sampled peak or hard limit.
An asynchronous headless Apply completes while 304 frames render over six seconds;
this is lifecycle evidence, not seamless loading or native-window FPS acceptance.

The same wider-water pipeline now covers all three prepared world sites with
coherent 2048²/64 m rain-on states (131.008 km sampled span). The original
mountain also has a 2048²/32 m alternative (65.504 km) for sharper local views.
These are independent saved states, not an outer-water splice around live Fluid.
The live 1k/16 m Fluid default and the terrain-only site path remain unchanged.

##### Workflow hardening qualification (2026-10-13)

Review: <http://starship:8801/terrain/landscape-hardening-qualified-20261013-m9s7qd/>.
The frozen release build passes 39 selected regression groups without skips,
ten headless compatibility/error controls, all four historical locked-camera
comparisons, and exact `auto` / mountain-local parity. Partial-cache fallback,
explicit legacy options, uniform Apply and custom-focus fixed Apply each match
their corresponding startup/control frame pixel-for-pixel. GPU audits confirm
unchanged bed/depth/decoded velocity and zero hydraulic dispatches.

All eight 1080p render-only repeats pass the unchanged 8 ms GPU / 4 ms CPU-service
gates: whole-scene GPU p95 3.508–4.076 ms, CPU service p95 at most 0.318 ms.
Steady device-local heap observations peak at 759.06 MiB. This is hardware
headless Vulkan with the solver stopped and no encoding/readback, not a native
window or live-solver FPS qualification. Timing differences from the previous
qualification are not claimed as a rendering speedup.

The full replacement sequence retains old/new overlap: 1292 MiB peak process
RSS, with a largest post-install GPU heap observation of 815.56 MiB (not a
continuously sampled peak). Wet CPU preparation takes 1.38–1.45 s followed by
0.51–0.54 s installation. Asynchronous Apply completes while 303 frames render
over six seconds; loading is not instantaneous or guaranteed hitch-free.

The original mountain / 32 m scene remains the recommended starting candidate.
Wider states remain useful coverage/stress alternatives, not finer local water.
The remaining promotion gate is joint native GUI/visual review: Site / Water
selection, Selected versus Showing, loading/failure feedback, Apply/reset and
visibility/motion/framing controls. No-argument startup still selects legacy;
default promotion and push were deliberately not part of this hardening pass.
Use the exact paths in
`outputs/terrain/wider-water-world-20261012-gqeluh/SITE/published-wide-64m.json`
(or `mountain/published-fallback-32m.json`) with:

```sh
rtk proxy build/release/projects/terrain/terrain \
  --terrain-site PATH/site.json \
  --terrain-site-water PATH/water-binding.json \
  --terrain-water-vertex-budget 4500000
```

The optional sidecar pins site/world recipe/source/capture identities without
modifying the site package. Capture source, centre, coverage and binary fields
are validated before drawing. The saved fields do not evolve; surface animation
is decorative. Missing or mismatched assets fail admission, never trigger a bake.
The unchanged default topology budget is 2M vertices; this explicit wider mode
retains at most 64 m exterior draw spacing with a bounded 4.5M budget.

The 128 m pilots are coarse controls. The 64 m states remove the old inner wet
rectangle, but lose some local detail; the 32 m alternative improves detail while
retaining a nearer water boundary. Huge localized pools, sheet flooding, bright
sun-facing reflective films and the finite outer terrain remain documented limits
under the deliberately exaggerated 512 mm/h rain. No default promotion or wider
live-solver/native-GUI performance claim follows.

Report: <http://starship:8801/terrain/wider-water-world-20261012-gqeluh/>.
See [world-wide water integration](world_wide_water_v1.md) for the bake/review
contract, measured costs, retained stress views and immutable-field evidence.

#### Prepared-landscape first use (promotion-ready)

The Terrain panel now uses human-readable Showing / Selected labels and an
explicit **Saved water - no solver running** or **Terrain only - no water loaded**
summary. Water coverage comes from the sampled capture grid; terrain-backdrop
coverage comes from the admitted site. Capture time and rainfall are labelled as
history, not current weather or editable rain. Surface animation changes saved
flow cues, never water levels or distribution.

Framing, Show water, Animate saved flow cues and Reset landscape camera remain
prominent. Loading identifies the requested landscape and retains the previous
scene until ready; upload may briefly pause rendering. Technical identities,
load timings, material diagnostics and coverage options are collapsible. The
menus update Selected immediately; Showing changes only after Apply succeeds.
Existing Apply/reset semantics and CLI/config ownership are unchanged.

Compact review: <http://starship:8787/reports/ebb9d3fb17214c5a8d6e4a25e44f9fcc0b722c3e9fbf631ef067f1e5d8388df2/>.
The existing report hub also serves the authoring export at
<http://starship:8801/terrain/landscape-first-use-qualified-20261013-1SCoIc/export/>.
The HTML-report publisher stores an immutable snapshot of the intended report
assets only. HTTP bytes and its actual view/visibility/video controls were
checked from Starship in a private headless browser; desktop/mobile screenshots
and a no-script fallback check are retained under the study root. This is browser
report evidence, not native Cubey GUI or Snap-network acceptance.

Frozen first-use qualification passes 41 selected regression groups, without
skips, the ten retained hardening controls, the no-argument legacy comparison,
foreign-working-directory launch, incompatible legacy-control rejection and
replacement lifecycle checks. The opening frame and all four historical wet
frames remain pixel-exact. Saved bed/depth/decoded velocity are audited unchanged
with zero hydraulic dispatches. The two ten-second clips compare animated flow
cues with a held presentation clock on identical hydraulic fields/camera;
compressed video frames are not an exact-pixel reference.

Four representative 32m/64m, 1080p render-only repeats pass the unchanged 8ms GPU
and 4ms CPU-service gates: GPU p95 3.384–4.071ms; CPU service p95 at most 0.326ms.
The solver is stopped, and timing excludes encoding/readback and 120 warmup
frames. These are headless scene timings, not native FPS, GUI cost, or a claimed
rendering speedup. Assets, shaders, solver, live Fluid defaults and lighting/
materials remain unchanged. Earlier diagnostic attempts are retained separately.

Joint native review is still required: opening framing/panel readability;
saved-versus-live explanation; Selected/Showing and loading/failure feedback;
wet/dry switching; water/motion controls; Local/Wider framing and camera reset.
Personal checkboxes in the browser checklist do not acknowledge approval.
After explicit approval, a separate default change can make `auto` the starting
selection while retaining missing-cache fallback and explicit legacy options.
Until then, no-argument startup remains legacy. This pass launches no native
GUI and does not promote a scene or push commits.

#### Wide-view rendering study (opt-in lighting trial)

The follow-up [wide-view study](../../docs/terrain-wide-view-study-v1.md)
separates distant terrain grain, actual saved storm flooding and finite coverage.
One inexpensive paired-ground lighting filter is retained as
`--terrain-readability filtered-study`; `refined` and startup defaults are unchanged.
It reduces distant normal peppering without changing geometry/water fields, but
does not remove shallow water sheets, extend water coverage or qualify far shadows.

Matched browser controls:
<http://starship:8787/reports/55f06e5da5dce2a094a9b5004154524074f6915b96ac0b34929cc317e77c9b85/>.
All 42 selected regression groups pass. Current/filtered render-only scenes stay
within the 8ms GPU / 4ms CPU-service gates; observed paired GPU p95 differences
are +0.026–0.041ms. Denser uniform geometry costs 8.748–8.773ms and is not promoted.
The close pair, raw statistics and deliberately wet finite edge remain visible.
Native GUI and default review are deferred; the next bounded study is distant
water coverage/shading, not a new solver or terrain bake.

### Captured-water landscape context (opt-in study)

Add `--terrain-water-context --terrain-water-capture-camera landscape` to the
captured-water invocation for wider ground from the same source. The core's
saved fields and B-spline ground/water stay unchanged. Static rectangular rings
join its exact 2x perimeter, with an exterior-only 128 m reconstruction collar
and progressively coarser source sampling. There is no overlapping radial ground.

The UI can toggle the context without changing hydraulic state. `landscape`
provides a supported scenic framing; `boundary` deliberately exposes a wet edge.
`--terrain-water-ground-view normal|clay|ownership` provides ground diagnostics
that hide water. Ownership shows captured ground in orange and context in blue.

This does **not** compute water in the surroundings: streams can visibly end at
the saved rectangle, especially in the boundary view. The outer source is also
finite and asymmetric around this capture. Do not describe a broad overview as
a seamless landscape-wide hydrology product. Ground dampness fades as a visual
weight over 64 m outside the core, not as invented water depth or persistent soil
moisture. Clouds remain disabled.

Both ground products render before water into shared opaque color/depth using
the accepted material/lighting code and one HDR transform. Context-on uses a
4096 shadow with bounded up-sun caster coverage for the core; far-context shadow
receivers remain unqualified. It does not reduce the core shadow to fit the
entire 61.41 km terrain. The original no-context path stays the default.

Report helper:
`python3 projects/terrain/review_captured_water_context_v1.py OUTPUT --phase capture|performance|report`.
No solver, terrain inference or native GUI is launched by this study.

The first context report is
`http://starship:8801/terrain/captured-water-context-20261012-aAoOLi/`.
Ten foundation/Fluid tests pass; all 8,184 CPU/GPU edge comparisons are within
0.49 mm. The retained context-off and Fluid captures remain exact. Context-on
whole-scene GPU p95 is at most 4.02 ms at 1080p on Starship, with 488 MiB of
whole-app device-local usage. The boundary view still reveals water cutoffs:
context is opt-in, not seamless full-landscape coverage or a promoted default.

### Shared surface-field study (2026-10-08)

Climate loading and optional surface math now belong to `cubey::asset` and
`cubey::terrain`; project headers retain compatibility aliases. Production
defaults are unchanged. `--terrain-surface-model correlated-surface` and the
GUI's **Source-aligned masks (study)** selection use a full-source descriptor
shared with Fluid 2.5D. It is cached with the render product and independent of
consumer crop, height offset and draw-mesh spacing. The old three models keep
their historical semantics. The companion is optional for the new descriptor;
without it, bounded landform-only potentials are used.

The new bare-substrate material recipe did not pass visual review and was
removed. This option retains the data/diagnostic experiment, **not** that recipe
or a claim of improved surface fidelity. See the surface boundary in
`docs/architecture/terrain-v1.md` and
`outputs/terrain/correlated-surface-v1-20261008-b2` for the comparison evidence.

The active path is fixed:

- regular external float heightfield with validated metadata, coverage, and
  exact payload SHA-256;
- deterministic selected placement over the unchanged source field, with
  unfiltered center and indexed-sample comparison controls;
- 200 m default foreground height, adjustable from 2-1000 m in the UI, and
  unrestricted orbit yaw;
- 50-1000 m live inspection orbit radius and unrestricted elevation; the baked
  clearance contract remains qualified only through 250 m;
- continuous seam-matched center with full radial rings, 16.384 km outer
  radius, and angular/outer render stride 3;
- cullable static sectors plus an optional foreground review sphere;
- flat and filtered planar procedural-detail material presentations;
- cached hardware-filtered directional shadows from the outer backdrop
  sectors, with texel-scaled receiver normal bias, a bounded low-sun confidence
  transition, and the continuous inner stage retained as a receiver only;
- shared physical atmosphere, a running daytime solar clock, depth-aware
  Cloud V1 composition, environment lighting, and HDR post;
- bounded 20% default terrain aerial perspective, adjustable from 0-100% for
  scene-specific distance haze without changing the shared atmosphere;
- height, slope, material, normal, edge, and ownership diagnostics.

The renderer does not modify the source shape. The center is regular terrain,
not a cutout, flattened stage, or radial source mask.

The 500 m baked stage remains the clearance-qualified far-field reference.
The 100 m lane is the explicit close stress view. Lower review heights
intentionally relax the clearance guarantee so hero and surface-level views can
expose topology, source, and material limitations.

## Generate The Asset

The canonical development field is a 2048 x 2048, 30 m seed-0 Terrain
Diffusion result. Generate it explicitly:

```sh
cmake --build --preset dev --target cubey_terrain_generate_default_asset
```

The target uses `CUBEY_TERRAIN_DIFFUSION_ROOT` when provided. Otherwise it
creates a pinned source checkout, Python environment, and data cache under the
Git-ignored worktree path `cache/terrain/tooling/v1`. Generated runtime files
are written to `cache/terrain/sources/v1/default`, shared by Debug and Release,
and are not committed. Build-clean targets do not own these paths. A complete
bundle is reused only after its manifest contract, finite values, and actual
payload SHA-256 pass validation; explicitly rebuild it with:

```sh
cmake --build --preset dev --target cubey_terrain_regenerate_default_asset
```

Normal configure, build, and test never download or generate this data. If the
default or selected asset is missing, both GUI and headless startup fail with
the generation command. There is no procedural fallback.

The dependency-free source-cache validator is part of normal CTest. The pinned
NumPy/Torch producer suite remains available through the explicit tool wrapper
when changing generation code; ordinary terrain runtime changes do not need to
initialize that environment.

The loaded source bundle is separate from the derived backdrop-product cache.
The runtime stores versioned compact CPU mesh/material products under
`cache/procedural/v1/terrain.backdrop.product`. Cache identity includes source
and optional climate SHA-256, placement, surface model, topology, stride, and
codec versions. A hit still validates and loads the source manifest and plans
placement, then decodes the product before following the normal GPU install
path. A miss or rejected entry regenerates and atomically republishes without
making cache IO a runtime requirement.

Force only the derived product to rebuild with:

```sh
rm -rf cache/procedural/v1/terrain.backdrop.product
```

This preserves the Terrain Diffusion source, checkout, environment, and model
data. Use `cubey_terrain_regenerate_default_asset` only when the external source
itself must be regenerated.

The optional climate companion for the canonical field and the five-region
cross-climate calibration pack are also explicit targets:

```sh
cmake --build --preset dev --target cubey_terrain_generate_surface_study_asset
cmake --build --preset dev \
  --target cubey_terrain_generate_climate_calibration_assets
```

The calibration pack is written under
`cache/terrain/sources/v1/climate-calibration`. It is evidence for the
experimental surface model, not a production asset or biome contract.

The committed source catalog defines one default and four optional natural
location presets:

```text
mountain-backdrop-1 (default)
alpine-range-1
mountain-valley-1
rolling-hills-1
rolling-lowland-1
```

Normal setup remains one preset and one Terrain Diffusion query. Optional
presets are generated independently, only when requested:

```sh
cmake --build --preset dev --target cubey_terrain_generate_alpine_range_1
cmake --build --preset dev --target cubey_terrain_generate_mountain_valley_1
cmake --build --preset dev --target cubey_terrain_generate_rolling_hills_1
cmake --build --preset dev --target cubey_terrain_generate_rolling_lowland_1
```

Each target queries its pinned location directly and writes an independent,
Git-ignored bundle under `cache/terrain/sources/v1/presets`. It does not rerun
the landscape scans or ranking studies. The recipe catalog, exact source
identity, and expected hashes are committed; generated raster data is not.

## Build And Run

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_terrain
./build/dev/projects/terrain/terrain
```

The GUI exposes runtime source selection across the startup field and available
generated climate-calibration regions, source provenance and dimensions,
runtime placement mode and raw-sample index, placement metrics,
orbit radius/elevation, foreground height and reset, foreground-sphere
visibility, flat/detail presentation, supported diagnostics, directional-shadow
state, terrain aerial-perspective strength, atmosphere controls, submitted
geometry, stable GPU timings, and shared cloud controls. Preparation diagnostics
identify cache hit/miss/rejection and report source, climate, placement,
load/decode, generation, encode, and store times independently.

Windowed startup also publishes placeholder lunar/night-sky textures while the
shared atlas runtime prepares cached payloads and uploads a complete
replacement. Headless capture waits for the same request before frame zero.
Atmosphere generation is therefore outside the first-present path without
introducing a second capture-only implementation.

Useful startup overrides:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-heightfield /path/to/field-or-heightfield.json \
  --terrain-placement raw-sample \
  --terrain-placement-index 2 \
  --terrain-foreground-height 500 \
  --terrain-camera-preset backdrop \
  --terrain-surface-detail filtered-detail \
  --terrain-aerial-perspective 0.2 \
  --terrain-shadows \
  --terrain-backdrop-azimuth 90 \
  --terrain-backdrop-orbit-radius 200 \
  --terrain-backdrop-elevation 24
```

Placement choices are `selected`, `raw-center`, and `raw-sample`. CLI values set
the startup placement; the GUI can stage another mode/index and apply it while
the app remains open. CPU resampling runs asynchronously, then the completed
cached product replaces the GPU meshes atomically and resets the orbit while
preserving foreground height. Prior GPU meshes retire after the latest submitted
frame completes, without a queue- or device-idle stall. `raw-sample` uses the
independent deterministic placement index and performs no quality rejection or
retry.

The source selector stages the same complete replacement for the startup source,
optional catalog presets, or available `hot-dry`, `hot-wet`, `cool-wet`,
`cold-dry`, and `cold-wet` calibration regions. It loads paired height and
climate manifests rather than recoloring the active geometry. Optional recipes
remain visible when their data is absent and report `not generated`,
`generating`, or `incomplete`; only complete bundles are selectable. The app
never launches Terrain Diffusion itself. If a region cannot satisfy the
requested placement, the current source remains active and the UI reports the
contract error. Calibration regions remain evidence rather than named
production presets. During replacement, the source panel names both the
resident terrain and requested preset, then reports heightfield, climate,
placement, product-preparation, and GPU-upload phases with elapsed time.

`backdrop-stage` shows the foreground sphere; `backdrop` hides it. Material
choices are `flat` and `filtered-detail`. Supported `--debug-view` values are:

```text
surface height slope clay normal classification-normal material-weights
ambient-visibility material-albedo material-normal material-roughness
vegetation moisture ambient-light direct-light sun-visibility projected-edge
stage-ownership
```

`--terrain-render-stride 1|2|3` is a reference-only startup diagnostic. It
rebuilds the same cached source product with a different fixed topology so
captures can distinguish source-shape defects from draw-mesh faceting. The
product default remains stride 3; this option is not adaptive LOD and is not an
interactive quality setting.

Retired source versions, profiles, weathering, LOD, tessellation, and local
terrain camera modes are rejected by the product app.

## Review

Generate the canonical visual matrix with:

```sh
projects/terrain/capture_product_review.sh
```

The script replaces `outputs/terrain/product-v1`, writes individual 1600 x 900
captures, an index, a manifest, provenance metadata, and a contact sheet when
ImageMagick is available. It covers clean and foreground views, flat/detail,
four headings, camera-envelope endpoints, neutral/raking light, and the
supported source/material/topology diagnostics.

Generate the selected-versus-unfiltered placement control with:

```sh
projects/terrain/capture_placement_control_review.sh
```

That separate pack compares selected placement, the raw source center, and raw
sample indexes 0-2 at matched headings and foreground heights. It also records
the exact placement metrics used by the review.

Selected placement evaluates a 500 m radius around the subject, with limits of
120 m local relief and 0.275 P95 slope. This covers the 300 m stage and the
clearance-qualified 250 m orbit with margin. Wider review-app orbits are stress
views and do not expand the accepted placement contract. Mountain/open
directional composition is a preferred ranking contract, not a source-validity
gate: non-mountain presets activate at the best locally safe candidate and
report `best available` composition in the UI.

Generate the isolated lighting and material candidate pack with:

```sh
projects/terrain/capture_lighting_material_review.sh
```

It writes `outputs/terrain/lighting-material-v1` with matched shadow controls,
four material headings, diagnostics, camera controls, provenance, and steady
plus forced-update GPU profiles. On NVIDIA hosts, a missed timing gate is
retried when `nvidia-smi pmon` reports concurrent compute work.

Generate the current rendering-acceptance pack with:

```sh
projects/terrain/capture_rendering_acceptance_review.sh
```

It uses the cool/wet selected source for 100/200/500 m framing, five sun
elevations, shadow controls, topology/material/lighting diagnostics, all five
climate sources, and steady plus moving-clock profiles. It writes
`outputs/terrain/rendering-acceptance-v1`. Profile lanes wait for external GPU
compute to become idle and retry when overlap is detected.

To rebuild contact sheets and metadata from a complete retained profile set
without running the GPU lanes again, use:

```sh
PROFILE_ONLY=1 SUMMARIZE_ONLY=1 \
  projects/terrain/capture_rendering_acceptance_review.sh
```

Generate the rendering-envelope and fixed-topology decision pack with:

```sh
projects/terrain/capture_rendering_envelope_review.sh
```

It writes `outputs/terrain/rendering-envelope-v1` with deterministic clear and
fair-cloud macro views, qualified and stress camera envelopes, matched stride
1/3 surface and topology diagnostics, and mean/p50/p95 GPU profiles. The pack
is a decision gate for LOD versus source refinement; it does not expand the V1
camera contract.

Generate the cross-climate surface evidence with:

```sh
projects/terrain/capture_climate_calibration_study.sh
```

It writes `outputs/terrain/climate-calibration-v1` with five generated climate
regimes, three matched surface models, fixed-scale source previews, final
surface diagnostics, profile metrics, invariant checks, and contact sheets. It
does not tune the model or change the production default.

Generate the matched Material V2 review in three steps:

```sh
projects/terrain/capture_material_v2_review.sh control
projects/terrain/capture_material_v2_review.sh candidate
projects/terrain/capture_material_v2_review.sh finalize
```

The control must be captured before changing the material shaders. Finalization
checks the frozen heightfield, cached product, topology, stride, and material
allocation; it then writes paired qualified, raking-light, stress, cloud, and
diagnostic sheets under `outputs/terrain/material-v2`.

Generate the accepted V1 visual-closure pack with:

```sh
projects/terrain/capture_visual_closure_review.sh control
projects/terrain/capture_visual_closure_review.sh candidate
projects/terrain/capture_visual_closure_review.sh finalize
```

It uses the canonical mineral-control source, selected and raw-center
placement, four headings, the day-to-night solar envelope, 100/200/500 m
framing, fair clouds, material and lighting diagnostics, and steady plus
moving-clock profiles. Finalization preserves source/product/topology
invariants, requires a pixel-identical flat control, and enforces the accepted
`1.10 ms` mean/p50 terrain composition budget.

The rejected daylight-form study records a marginal mineral/ambient tuning pass
and an uneconomical source-normal texture experiment:
[`docs/notes/terrain-daylight-form-v1.md`](../../docs/notes/terrain-daylight-form-v1.md).
The resulting source-versus-rendering diagnosis, effort boundary, and stop
conditions are recorded in
[`docs/notes/terrain-visual-fidelity-strategy.md`](../../docs/notes/terrain-visual-fidelity-strategy.md).

## Tests

```sh
cmake --build --preset dev --target \
  cubey_core_tests \
  cubey_project_terrain \
  cubey_project_terrain_config_tests \
  cubey_project_terrain_raster_climate_source_tests \
  cubey_terrain_backdrop_product_tests \
  cubey_terrain_backdrop_product_cache_tests \
  cubey_terrain_backdrop_placement_tests \
  cubey_terrain_directional_placement_tests \
  cubey_project_terrain_product_adapter_tests \
  cubey_project_terrain_shadow_tests \
  cubey_project_terrain_surface_model_tests

ctest --preset dev --output-on-failure \
  -R '^(cubey_core_tests|terrain_.*_tests|terrain_backdrop_headless_writes_png(_stats)?)$'
```

The focused suite verifies the narrow runtime config, raster contract and
filtering, deterministic topology and seams, selected/raw placement, and the
placement-stage camera/coverage contract. It also checks directional-shadow
coverage and cache invalidation. Product captures use the canonical asset
deliberately; ordinary unit tests use small analytical or temporary fixtures.
The real headless terrain smoke uses the deterministic tracked raster under
`tests/assets/terrain/backdrop-smoke` and verifies a nonblank PNG without a
network or generation dependency.

## Studies And Boundaries

Historical visual controls and hydrology work live under `studies/terrain` and
are excluded from default builds. Enable them with:

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies
```

`projects/planet` owns planet-scale terrain. The paused hydrology study owns
regional drainage experiments. glTF Viewer is the first external consumer, and
the backdrop API now lives in `include/cubey` and `src/cubey`. Additional
adapters remain deferred until a concrete scene requires one.

See [Terrain V1 Runtime](../../docs/architecture/terrain-v1.md),
[Terrain Product Promotion](../../docs/notes/terrain-product-promotion.md), and
[Terrain Project Map](../../docs/notes/terrain-project-map.md). The default and
optional generation boundary is documented in
[Terrain Source Preset Contract](../../docs/notes/terrain-source-preset-contract.md).
The experimental climate path is documented in
[Terrain Climate Surface Model Research](../../docs/notes/terrain-climate-surface-model-research.md)
and
[Terrain Climate Calibration V1](../../docs/notes/terrain-climate-calibration-v1.md).
The rejected broader climate response is retained only in
[the terrain archive](../../docs/archive/terrain/climate-response-v1-1-rejected.md).
