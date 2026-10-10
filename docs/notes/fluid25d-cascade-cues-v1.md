# Fluid 2.5D opt-in cascade cues V1

2026-10-09. A rendering-only hint of cascading/tumbling water, not detached falls
or transported foam. All presets retain zero strengths; previous rapid and
shallow-water options remain. No solver, terrain, footprint, mesh or particle edit.

## Controls and mechanism

Scenic material v2 adds two bounded 0..1 controls: `water_cascade_strength` and
`water_landing_strength`, both default zero. GUI: Cascade streaks / Landing foam
cue. Both reuse `water_rapid_scale_m` (64..384 m) as an artist procedural scale.
The existing `water_rapids` uniform vec4 now explicitly packs rapid strength,
pattern scale, cascade strength and landing strength; layout remains 368 bytes.

Cascade: the existing directional wet/speed/steep-flow activity gates a dedicated
fixed 3D value-noise pattern, elongated vertically. Physical height is used, so
steep faces do not collapse into horizontal projection. Horizontal visual velocity
is lifted onto the physical surface with a guarded gradient, artist speed scaled
3x and bounded to 6..24 m/s (stationary remains zero). This is not a physical speed
claim. Two spatially dephased 8 s layers reset at zero weight and repeat across the
existing 1024 s clock wrap. Coordinates are translated, never rotated by local flow.
Derivative-based filtering suppresses unresolved procedural features.

The applied diffuse replacement is activity × strength × (0.94 × pattern),
up to 94%, with no constant pale floor and darker water gaps. The breakup study
removed the former 0.16 floor, retaining scale, motion and peak. Foam is
sun/SH-lit and shadowed, not emissive.
Local roughness rises toward 0.72. Existing optics remain beneath the artistic mix.
An additional physical-depth fade over 0.08..0.20 m excludes centimetre-scale
hillside films from both new cues. The first candidate without it whitened broad
wet-ground areas and was rejected; its captures/source pins remain in the report.

Landing: two upstream bilinear queries at one and two cell spacings along local
filtered horizontal flow compare upstream surface descent with local physical
grade. Wet moving water entering a flatter surface can receive foam; a uniform
inclined plane, stationary water, uphill receiver or dry/off-map support cannot.
All four corners of each upstream query must be wet. This conservative local
heuristic is not an impact detector or a prediction of foam lifetime. Replacement
is bounded to 75%, patterned by the same animated scalar, within existing coverage.

New views append 15 `cascade-weight`, 16 `landing-activity`, 17 `no-landing-foam`.
View 17 is full-shading cascade-only, honoring the existing shallow coverage fade.
Views 1..14 retain their previous meaning; component diagnostics ignore new mixes.
Activity colors use Scenic tonemapping, not linear readback.

## Evidence

Report: `outputs/fluid/cascade-cues-v1-20261009-If4tBN/index.html`.
The report distinguishes static arithmetic, synthetic renderer controls, held
presentation motion, accelerated saved-field replay, measured render-only GPU
cost and human visual acceptance. No automated result is a realism claim.

Final 7/7 focused CTests pass, including 390 GPU helper controls, 18 retained rapid
render controls and 23 new render controls. Default/off and retained rapid stream
and lake PNGs remain byte-identical to the preceding sealed pass. Final stills and
held/advancing replay source/runtime pins match; input recording trees are unchanged,
with hydraulic dispatch counts zero. The archived manual synthetic stage predates
the phase-filter correctness fix and is not the final runtime receipt.

Visual verdict: partial improvement. Left changes clearly, right modestly, middle
remains subtle. Movement helps, but a held pale patch can still read as paint and
the broad/coarse bed-following outline remains. Landing ablation changes only
978/921,600 pixels in streams (0.106%), 182 in lake and 76 in wide. Do not credit
landing with the main improvement or call the marked ribbons solved.

No new resources or passes. Pattern noise adds ALU; landing adds up to two bounded
upstream queries. Off skips the new work. Three alternating render-only batches on
RTX 5070 Ti at 1280x720 measure stream GPU p95 0.904 -> 0.999 ms (+0.096 ms, 10.6%)
and lake 0.692 -> 0.748 ms (+0.057 ms, 8.2%), excluding solver and encoding. These
are medians of run p95s, not end-to-end interactive frame times. Human GUI/motion
acceptance remains deferred; retain opt-in without default promotion or push.

## Bounded breakup follow-up

`outputs/fluid/cascade-breakup-v1-20261009-OVOd5e/index.html` compares the original,
zero-floor ablation and one rejected extra coarse-coverage mask. The mask mainly
darkened sections without making convincing broken water; it is not retained.
The final shader only keeps the zero-floor cleanup under the existing control.
Temporary RGB diagnostics are archived, not permanent shader/UI options.

Actual fragment-stage samples show strong resolved detail on the left, correcting
the earlier filtering hypothesis there. Middle core has weaker activity; its
shallow segments also have weaker depth support. Do not globally relax the depth
gate to make those films white. Tonemapped samples are not linear readback.

The broad bed-following footprint remains the main presentation limitation.
This is a modest cleanup, not a ribbon or waterfall solution. Final validation,
unchanged-mode parity, held/advancing clips and render-only cost are recorded in
the follow-up report; earlier measurements above belong to the preceding shader.
The follow-up passes 7/7 focused CTests, including 406 GPU helper outputs. Thirteen
default/previous-rapid/landing-only stills remain byte-identical to the frozen
reference. Encoded motion contrast increases; this is not a stability fix. Initial
and repeat p95 cost sets differ substantially, so retain both and do not claim a
speedup or strict tail-budget proof. No new noise layer remains.
