# Hillside motion and readability V4

## Scope and reading the scene

Continue the accepted immutable Terrain Diffusion hillside and the validated
[V3 conservation/transport baseline](fluid-25d-hillside-conservation-transport-v3.md).
The dry start, native 30 m cells, 256/512 crops, source location, continuous
100 m3/s supply, dt 2 s / 16 substeps, gravity, damping, and outward-only crop
edges remain unchanged. The 512 crop is wider, not finer. There is no authored
drain, rain, terrain editing, erosion, prefill, or source-rate retuning.
`virtual-pipes` remains the default; these controls are opt-in finite-volume.

In the retained V4 captures, the green ring marks the five-cell upland input.
The current renderer instead highlights each actual input cell with a small
translucent green cube (see the follow-up below). Blue water descends and stores
in the terrain. Pale dots are observational motion markers released at that
input; their movement follows the accepted simulated depth-averaged velocity.
Short trails show recent travel. They are not rigid floating objects, waves,
foam, a routed river path, or additional water. Slow movement in the broad
storage area is an observed velocity, not a presentation-speed correction.

The optional dye colors input during physical minutes 60-62. Clear water keeps
entering afterward. The hillside-only palette uses a fixed logarithmic
concentration scale: 0.1%, 1%, 10%, 100% of injected concentration, with a 0.01%
clear-water floor. Color is neither speed nor depth. Eighteen percent of the
underlying carrier color remains even at full concentration. Other scenarios'
legacy dye palettes are unchanged.

## Presentation implementation

Markers occupy a separate 64 KiB GPU buffer. Compute and vertex shaders bind
hydraulic fields read-only; no hydraulic shader can write this marker buffer.
One marker is released every four outer fixed steps, with deterministic source
jitter and a bounded 512-slot lifetime/reuse policy. Rebirth clears old history
instead of drawing a line back to the source. Motion uses wet-normalized
bilinear velocity sampling and midpoint integration with at most quarter-cell
internal displacement. Dry support, crop exits, or an excessive unsupported
displacement retire the marker rather than inventing speed or bridging a bank.
Trail rendering separately checks wet support and omits overly long chords.
Marker height matches the water mesh's actual triangle diagonal, rather than
a bilinear surface that can lie below it on curved cells. The independent
0/0/0/4 m curved-cell counterexample has a 2 m mesh-center height but only
1 m bilinear height; using the latter can wrongly occlude a moving marker.

Reset clears all presentation history. A sticky rejected hydraulic update
freezes marker bytes and displays the last accepted position. Windowed display
interpolates between accepted positions using the fixed-step pacing remainder;
pause holds that fraction, and a compute advance holds the completed position
until regular playback updates again. There is no presentation-time advection
that could keep moving while the solver is paused.

Explicit source, downstream-branch, and whole-domain camera presets are
render-only. Source framing covers approximately 1.8 km; the legacy marker-free
launcher retains its established approximately 3.2 km context. The historical
V4 ring was reduced to keep it from enclosing most of the storage basin; the
current cell cubes replace it entirely. Shoreline/grid smoothing is outside
this pass.

## Continuous GUI review

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py \
  --domain 512 --markers --dye --view transport-inspection --developed
```

`--developed` executes every ordinary solver step from dry start to physical
minute 55 with visible progress, then resumes continuous 8x playback. It is
not a jump in dt or an injected developed state. Dye begins five physical
minutes later. Space pauses/resumes, R restores dry start, and the panel can
hide markers or switch camera. The older ten-minute advance-and-pause remains
under the collapsed Inspection tools control. Closing the window stops review;
there is no automatic two-hour GUI stop.

The bounded headless captures cover two physical hours in 120 seconds: 3600
frames at 30 fps, 60x playback. Their excerpts cover physical minutes 0-15,
30-45, and 55-85. Their physical-time labels retain the convention that frame n
shows the state after `(n+1)*2` seconds. Headless captures show accepted states;
normal windowed playback additionally interpolates markers between them.

## Validation and evidence boundary

Evidence root: `outputs/fluid/hillside-motion-v4-20260930-OqfDt7/`.
The [review guide](../../outputs/fluid/hillside-motion-v4-20260930-OqfDt7/REVIEW.md)
routes to final short clips and explains how to read them. The final
`final-surface/review/review.json` passes its full-horizon gate, rechecking
the complete profiles and all four cameras' raw, labelled, and excerpt
receipts (20 video files). No source-only fallback was used.
The final mesh-aligned variant has its own `final-surface/` evidence; earlier
V4 runs remain retained and are not claimed to render with the later shader.
`run_hillside_motion_v4.py` separates smoke, profiles, captures, and review.
Phase directories are exclusive and artifacts are hashed; full captures
require the same app/input identity as passing profiles. The matched source
pair differs only by the marker flag and each run's output destination.

Actual GPU shader controls verify uniform-velocity displacement, resting
water, dry-strip retirement, rejected-step byte freezing, boundary retirement,
and deterministic reset/batch grouping. CPU controls verify frame-rate
independent display timing, pause/reset, palette mapping, option restrictions,
and evidence-policy failures. All 136 non-windowed integrated CTests and 164
Python tests passed on the final mesh-aligned V4 build. The 20 registered
interactive/windowed smokes were outside that suite; the actual hillside
windowed pacing check below is separate. The curved-cell surface-height
counterexample is covered by the integrated controls.

The automated offscreen windowed Vulkan/Xvfb check at 1280x720 ran 900 frames
per 512-domain case. Both cases computed to 3300 seconds and resumed with 797
continuous/unpaused frames, reaching 3420 seconds without markers and 3416
with markers. Requested 8x playback measured 7.956x/7.910x respectively, with
zero dropped backlog. After warmup, median frame intervals were
17.612/17.404 ms and p95 intervals 21.748/21.705 ms. These short sequential
runs demonstrate practical pacing, not a controlled statistical performance
comparison or a sustained all-camera benchmark. GPU timestamp scopes are
command-stream spans, not an isolated whole-overlay incremental cost.
The final marker-on update/draw scope medians were 0.006/0.004 ms. These
measurements and full before/after app, shader, and input identities are in
`final-surface/windowed-performance.json`.

The full profile gate passed: each of the 256 no-dye and 512 dye runs exactly
matches all 421200 shared non-timing values against V3, including both hydraulic
fingerprint words at every one of 3600 fixed steps. Only marker-state metrics
are added. Maximum absolute water residuals remain 0.013719/0.018935 m3;
the 512 tracer residual remains 0.000459 m3 with the exact 12000 m3 pulse.
Water/tracer tolerances are unchanged. Retained
V3 strict CPU/GPU results are historical evidence bridged through unchanged
finite-volume SPIR-V/CPU-source hashes and exact new profiles; no V4 long strict
rerun, all-substep parity, or two-hour strict 512 comparison is implied.

The first driver report stays rejected: the historical V3 dye validator
requires identical hydraulic metric sets and initially rejected the newly
added marker category. Complete V3/V4 comparisons prove marker-only additions
before excluding that category from the historical dye gate; no hydraulic or
tracer metric is hidden. `--phase profiles --revalidate-profiles` verified the
same successful child executions, exact commands, before/after identities,
and all artifact hashes without rerunning or replacing them. Its passing
`profiles/revalidated-profiles.json` pins the retained rejected report's hash.
The later `final-surface/profiles/profiles.json` passes directly using fresh
complete child runs; it does not rely on that recovery.

Marker diagnostics reach 1252 m from the source, with a maximum of 488 active
markers; the bounded pool has 482 active at two hours. Lifetime reuse can make
the maximum marker distance decrease when an older marker disappears. That
does not mean the water front retreats. Markers show sampled travel, whereas
V3's approximately 5.2 km wet-front reach measures wet cells, not parcel travel.
Final headless GPU solver-span medians were approximately 0.594 ms (256) and
4.069 ms (512), and marker-update medians 0.004/0.008 ms. Full diagnostic jobs
took 209/798 wall seconds because they read and analyze fields every step;
those wall times are not interactive simulation costs.

The raw short MP4 omits its last packet's display duration, making container
average fps misleading. Media verification now checks every frame timestamp
against `n/30` within one microsecond and exact frame count/dimensions, retaining
the original rejected smoke and its probe receipt. A separate corrected smoke
passes; this change does not loosen numerical gates or hide a dropped frame.

Human animation/live-GUI readability acceptance remains a separate review.
This experiment also retains native-30m grid-shaped banks, depth-averaged
motion, first-order transport diffusion, and possible marker loss on thin wet
support. It is not a calibrated natural-river hydrology model.

## Follow-up: actual forcing-cell cubes

The current renderer replaces the large source/drain rings with one translucent
cube per positive source or explicit sink cell: green for input, amber for
removal. Each cube is one cell wide and high, centered horizontally on the
cell and vertically at its terrain height. Terrain height exaggeration moves
the center but does not stretch the cube. Buried portions are depth-tested;
shared side overlaps are omitted so adjacent cells do not stack transparent
faces. Cube height is a visual highlight, not water depth or the physical
extent of the solver's source. Open crop boundaries and expected-exit
observation windows are not explicit sinks and do not receive amber cubes.

The separate [cube review guide](../../outputs/fluid/forcing-cubes-20261001-wxUEOW/REVIEW.md)
contains matched hillside and authored source/drain captures, a closer source
view, and offscreen windowed Vulkan checks. Its developed hillside check
matches all 123 retained V4 numerical/marker metrics at frame 2249 (4500 s).
The cube renderer has a read-only instance buffer and changes neither forcing
nor hydraulic shaders. These newer captures do not replace the historical
V4 media or constitute another full-horizon strict solver comparison.

Commit review preserves the hash-pinned CPU oracle byte-for-byte, including
its internal-face lambda's historical formatting. The V4 runner deliberately
requires that source identity to bridge retained strict V3 evidence; cosmetic
formatting is not grounds to relax the gate. Human animation/live-GUI acceptance
remains pending.
