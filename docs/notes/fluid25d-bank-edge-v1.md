# Fluid 2.5D quiet water-edge spike

October 9–10, 2026. Render-only, opt-in, no numerical-default change.

## Purpose and boundaries

The owner asked whether subtle uneven banks, with a weaker flow-linked moving
water edge, could make the existing mountain rain demo feel less grid-authored.
The simple test is B-spline alone versus a fixed inset versus fixed plus motion.
Do not move terrain, invent dry-ground water, bake a new sidecar, introduce an
erosion solver, or add loud white bank stripes. The existing Macro water/fleck/
gentle-foam appearance remains unchanged.

The report is
[`outputs/fluid/bank-edge-v1-20261009-GLWoCh/index.html`](../../outputs/fluid/bank-edge-v1-20261009-GLWoCh/index.html),
served at `http://starship:8801/fluid/bank-edge-v1-20261009-GLWoCh/index.html`.
Inputs are the retained rain512 streams at saved 3600 s and natural-lake
recording at 27000 s, with a separate wide runoff camera. They are 512×512,
30 m cells. The tool uses saved native fields, not live hydrology.

## Reuse, not a new solver

[Cyanilux's original shoreline breakdown](https://www.cyanilux.com/tutorials/shoreline-shader-breakdown/)
demonstrates distortion of a shoreline gradient with noise and time, while
calling out the slope/view limitations of depth-based beach effects. We borrow
the rendering idea, not its stylized beach waves or source implementation.
[Valve's SIGGRAPH 2010 flow-water presentation](https://advances.realtimerendering.com/s2010/Vlachos-Waterflow%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf)
provides the established phased-advection/noise-breakup approach. Cubey already
has world-space PCG noise and dual-phase flow helpers; the experiment reuses
those without importing external shader code or textures.

Neither reference implies that noise repairs a coarse terrain/water intersection
mesh. It is an artistic edge treatment layered on existing reconstruction.

## Initial quiet implementation (historical)

Four bounded material-v2 controls (all metre units):

- `water_bank_irregularity_m`: 0–8, default 0.
- `water_bank_motion_m`: 0–3, default 0.
- `water_bank_scale_m`: 8–128, initially 48.
- `water_bank_band_m`: 1–24, initially 12.

The helper derives the physical XZ depth gradient from raster derivatives with
a relative determinant guard. Its local eligibility requires a shallow/film
sample next to deeper water. World-space rotated two-band noise produces a
fixed water-side inset. The moving part uses filtered velocity, the existing
8-second two-phase helper, broad spatial dephasing, and the pause-aware
presentation clock. Zero-speed water has no added time-varying inset.

The inset is limited to 15% of the native cell width and 20% of local supported
depth divided by gradient magnitude. An edge band and screen-footprint fade
suppress broad or unresolved changes. It changes shaded alpha only, including
the displayed thin-film fade. Geometry, physical depth and velocity, normal/
optical inputs, terrain, masks and numerical fields are not edited. There is no
additional pass, texture, buffer resource, mesh or solver work (just an appended
uniform vec4 and fragment calculations when enabled).

`bank-edge` is the eligibility diagnostic; `no-bank-edge` restores full water
shading while preserving separate fleck/rain/dot drawing. Existing coverage,
depth-band and component diagnostics bypass the experiment. This intentional
diagnostic separation means the coverage diagnostic does not display the
experimental alpha; use `bank-edge` and full-shading ablation for it.

## What the iterations established

The first strict wet/dry support test was a no-op in both recordings: every
native cell carries a rain film. The stream minimum depth is approximately
6.38 mm, the lake minimum 2.32 mm; no qualifying quad mixed a ≤2 mm cell with
≥20 cm water. Requiring a true solver-dry cell therefore missed the displayed
bank. That sealed scout and its zero-change pixel accounting remain in the
bundle; they are not positive visual evidence.

The next probe allowed ≤5 cm film beside deeper water, but trimmed only true
wet-threshold coverage. The final candidate also trims the existing 2–50 mm
opacity fade. Native data and other diagnostic/optical inputs remain unchanged.
Both superseded versions are retained separately, with source/runtime pins.

## Initial quiet recommendation (historical)

Do not promote the new effect by default or call it a general bank fix. With
fixed 3 m / moving 1 m requests, close lake changes affect about 0.17% of image
pixels and close streams about 0.05%; even the stronger 8+3 m stress probe has
limited impact. The overview is pixel-identical at all three strengths because
the effect becomes unresolved. Pixel counts establish that the effect exists,
not that it is visually worthwhile.

B-spline provides the main existing smoothing benefit, with its known apparent
width/gap tradeoffs. This experiment remains available for close-camera review;
it does not replace B-spline or marching-squares coverage. Owner GUI acceptance
and default promotion are not part of this spike. See the report's final motion,
regression and timing evidence for the completed evaluation.

## Validation and bounded cost

Thirteen focused contract/material/review/C++ suites and three GPU suites pass.
The latter include 758 analytic controls (128 new), 25 archive-independent full
renderer captures, and recording GPU byte-immutability checks. The new renderer
fixture also covers a deeper channel surrounded by a positive rain film, not
only true dry banks. These controls establish bounded behavior and unchanged
diagnostics, not subjective visual acceptance.

Actual no-tuning Macro/reference images are pixel-exact against the published
checkpoint in streams, lake and overview. B-spline baselines match in both close
views. Full-shading ablation is pixel-exact in both close views, with accepted
flecks enabled; a separate positive-count visible-rain ablation also matches.
The final still/motion manifests retain source/runtime/input hashes. No native
input tree or numerical-default change is made.

Two serial forward/reverse timing batches each capture 120 frames per view/
treatment, with 12 warmup frames excluded. At 1280×720 on Starship's RTX 5070 Ti,
whole-presentation medians span 1.00–1.61 ms. Lake moving medians are 1.04–1.09 ms
versus approximately 1.00 ms baseline. Stream timing and p95 vary substantially
between batches; lower enabled samples do not prove a speedup. This is bounded
render-only GPU timing, not solver/startup/CPU encoding cost, nor a strict
percentage-overhead guarantee. The report links full spans and source pins.

## Reproduction and evidence interpretation

```sh
rtk proxy uv run --with pillow python projects/fluid/fluid_25d/review_bank_edge_v1.py \
  --out outputs/fluid/bank-edge-<fresh-id>
rtk proxy uv run --with pillow python projects/fluid/fluid_25d/review_bank_edge_v1.py \
  --out outputs/fluid/bank-motion-<fresh-id> --mode motion --seconds 16
```

The tool rejects reused output directories, pins runtime/shaders/source/native
input trees, verifies upload controls and zero hydraulic dispatches, and labels
held versus advancing fields. Browser MP4s are separate H264 transcodes with
frame-count/fps/size checks. Compression changes are not native pixel parity.
Existing flecks/foam animate in every variant; their movement is not isolated
proof of bank motion. Analytic GPU controls establish local bounded motion;
matched full-scene clips establish whether it is useful at these cameras.

The local support gate can still inherit native-quad transitions. This is not
topology-preserving geometry reconstruction or a signed-distance shoreline
field, and cannot guarantee continuity for arbitrary terrains/velocities.
Avoid expanding this bounded spike into another general reconstruction effort
without a separate design decision.

## October 10 follow-up: deliberately stronger comparison

The owner found the first comparison hard to distinguish and requested a more
pronounced version. The original quiet results above remain historical evidence;
the new report is
[`outputs/fluid/bank-edge-bold-20261010-en2A9T/index.html`](../../outputs/fluid/bank-edge-bold-20261010-en2A9T/index.html),
served at `http://starship:8801/fluid/bank-edge-bold-20261010-en2A9T/index.html`.
It includes full cameras and equally enlarged near-bank still/video crops.

The same controls now permit fixed inset up to 24 m, moving variation up to 12 m,
and edge band up to 64 m. The old fixed ≤8 m / moving ≤3 m behavior is preserved.
Above those strengths the cap relaxes smoothly, at most to 45% of cell width and
60% of local depth-gradient support. Scaling the noise amplitude before final
clamping keeps an exaggerated request from flattening the pattern to a uniform
maximum inset. The comparison uses fixed 24 m, moving 0 or 12 m, noise scale
128 m and band 64 m; eligibility and viewing fades remain active. These are
requested strengths, not measured edge offsets. No extra resource/pass or
numerical field change is introduced. Every preset still has zero bank strengths.

Strong fixed and strong + motion now visibly trim the near shore and make it
uneven. The stronger moving still changes 1.549% of lake pixels and 1.382% of
stream pixels versus the B-spline-only still, compared with 0.171% / 0.052% for
the old quiet treatment. Overview changes are smaller (0.366%). This accounts
for effect size, not quality. The stronger treatment also exposes a dotted/steppy
fringe in places and can narrow or break apparent thin channels. Motion remains
local and less immediately readable than the existing foam; do not promote this
as a general stepped-bank fix or a default without owner review.

Three current focused CTest suites pass. Analytic GPU coverage expands from
758 to 790 cases, checking the expanded cap/time variation and inactive deep
water / isolated thin sheets. All six no-treatment and earlier-quiet stills
(lake, streams, overview) are pixel-exact against the preceding report. New
source/runtime/input pins, unchanged recording GPU fields and zero hydraulic
dispatch receipts are retained in the still/motion manifests. This follow-up
does not rerun or claim a new whole-presentation performance result. Cropped
clips are labelled 2× nearest enlargement of H264, not extra resolution or
isolated proof that the bank is animating. Nothing is committed or promoted as
part of this stronger visual trial.

## October 10 correction: perturb the reconstructed shoreline

Owner review identified the cell-shaped cutouts in the strong comparison and
clarified the intended composition: B-spline-smoothed display shoreline, then
noise and weaker flow-linked variation. The problem was a hybrid: B-spline
display depth combined with raw four-corner eligibility/extrema and triangle
depth derivatives. The prior strong result is not recommended or promoted.

The correction reuses `fluid25d_bspline_sample` in enabled B-spline bank shading
for continuous depth and analytic physical XZ gradient. Support and cap use a
continuous one-cell linear depth estimate (`h + cell * |gradient|`), not native
quad minima/maxima. This is an artistic locality estimate, not a signed-distance
shoreline or measured channel capacity. The existing smooth band, noise and
flow phases remain. Inside that band, coverage blends toward reconstructed
depth before subtracting the inset, bounded by the original displayed depth so
the effect cannot reveal a larger wet area. Off strengths and raw/component
views bypass it. Reference mode retains its own displayed depth/gradient; it
does not acquire cubic geometry. No numerical data/default or mesh position is
changed. Enabled B-spline shading adds one 16-sample reconstruction evaluation,
with no new texture/buffer/pass; a new performance result is not claimed.

The report is
[`outputs/fluid/bank-edge-continuous-20261010-YG9GRH/index.html`](../../outputs/fluid/bank-edge-continuous-20261010-YG9GRH/index.html),
served at `http://starship:8801/fluid/bank-edge-continuous-20261010-YG9GRH/index.html`.
It switches previous/corrected fixed and moving strong treatments at matched
cameras and clip times. The shadowed near lake shoreline is visibly more
continuous and rectangular cutouts are reduced. Finite-mesh/raster edges remain,
and the exaggerated dose is not proposed as the final subtle appearance.
The previous captures and first derivative-only correction are retained, not
overwritten. Final evidence is explicitly under `final-stills`, `motion` and
`renderer-controls-final`; earlier `stills` and `renderer-controls` are superseded.

Three final focused CTest suites pass, including 918 GPU controls. The 128 new
reconstructed-field samples straddle native grid lines, refined half-cell lines
and mesh diagonals with strong fixed/moving requests. Production cubic
depth/gradients match independent CPU evaluation; inset differences shrink with
10x smaller sample separation rather than retaining a boundary jump. These are
sampled continuity checks, not a universal topological guarantee. A float-cap
check permits 1e-5 m roundoff. All 37 synthetic renderer controls pass, including
pronounced dry/isolated-film/uniform-water exclusion, stationary fixed/moving
identity, retained component views and full-shading ablation. Capture receipts
verify unchanged uploaded fields and zero hydraulic dispatches. The three
no-treatment stills remain pixel-exact; old quiet enabled pixels intentionally
change because the field mismatch is fixed at every strength. No default
promotion, owner GUI acceptance, commit or push is part of this correction.

## Owner decision: retain as an option

The owner reviewed the corrected result as better but not perfect and requested
retaining it as an option rather than promoting it. Both bank strengths remain
zero in every preset; the existing bank reconstruction choices and defaults
remain unchanged. The GUI now explicitly labels the treatment optional/off by
default and has a disable button that resets both strengths without changing
reconstruction, noise/band settings or the other water materials. This is
acceptance as an optional visual treatment, not a general shoreline fix or
owner GUI validation. The captured reports retain their original source/runtime
pins; this follow-up changes GUI wording/reset controls, not the shader.

## October 10 follow-up: the remaining left-side steps were wet ground

The owner's annotated stream crop showed smoother right-side water banks but
cell-shaped grey patches around the small left-side pools. Matched terrain-only
and colour-only diagnostics established that those patches remain with water
hidden and disappear when wet-ground darkening is disabled. B-spline geometry
was combined with native-triangle depth in the terrain's rain-film material.
Deep water hid the mismatch; shallow film exposed it. A 4× display probe rounded
small pool outlines but did not remove these material patches.

Both Scenic terrain fragment paths now share a wetness helper. B-spline mode
uses the existing analytic cubic display depth; reference mode retains native
triangle depth with its existing arithmetic. Colour and roughness use the same
unchanged current-film weight (wet threshold → 12 mm onset, 20–50 mm fadeout).
There is no new material knob, pass, texture, solver work or geometry change.
Bank noise and B-spline itself remain optional. Small blue-pool mesh faceting is
not addressed by this material correction.

The [wet-ground report](../../outputs/fluid/bspline-wet-ground-20261010-3haWIL/index.html)
shows matched close stream, terrain-only, colour, roughness, lake and wide
views, with noise off. The marked grey staircase is substantially reduced;
the recommendation is to keep the correction within selected B-spline mode.
Four triangular-reference captures, including legacy material, are pixel-exact.
Only the two terrain fragment shader binaries changed: water/geometry/hydraulic
shader binaries and the application executable are unchanged. Native recordings
remain immutable; replay capture receipts validate uploads and zero hydraulics.

Eight focused CTests pass, including 1,046 analytic GPU controls, the existing
37 optional-bank renderer controls and recording immutability. The 128 new
controls cover sampling selection, thresholds, edge clamping and continuity
of both colour/roughness weights across native/refined/diagonal seams. An initial
source-guard failure was an outdated variable-name assertion from the preceding
bank ablation; its result is retained and the guard now checks the current alias.

Three warmed 720p batches per build measure after medians of 1.25–1.26 ms, versus
variable before medians of 1.24–1.99 ms. These sequential, non-isolated results
are not a speedup claim or a strict overhead guarantee. Captures and automated
checks are not owner GUI acceptance. The owner subsequently reviewed the
captures as much better and requested review and commit. Retain the wet-ground
correction whenever B-spline is selected; noise remains optional and off by
default. The renderer/tests and reproduction notes are committed separately.
