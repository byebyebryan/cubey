# Guardian and driftwood refinement evidence

Date: 2026-10-22

Status: implemented, qualified and delivered for visual review under the
[refinement plan](sdf-guardian-driftwood-refinement-plan.md). The first-pass
scenes, presets and published four-subject gallery remain intact.

[Open the matched refinement gallery](http://starship:8787/reports/d66a4ee7ce92dd2f255e170b9dc67676551a1f5efec099ec99ccda304a4d7973/).
It contains material/clay comparisons from every inspection angle, all 210
committed states, two construction/orbit films and twelve chapter links.

## Curve comparison and shared correctness

`outputs/sdf/refinement-curves-final/` compares the same horn and trunk with
linear versus monotone Hermite radius interpolation, small segment blends
(0.0012 and 0.006), and a denser hard-union chain. Native views retain fixed
field, camera and lighting settings; direct-expression strips are recorded
separately. More blending produces visible corrugation, especially near narrow
tips, and is rejected. Smooth width interpolation and a modestly denser hard
chain are selected. They improve the approximation but do not make an exact
curve sweep; rear/grazing views still expose some segment transitions.

The small-blend fixture exposed a CPU/GPU part-owner mismatch at a near tie:
CPU part 7 versus GPU part 5, with no reported distance or material-color error.
Smooth operations previously switched owner at `h < 0.5`, while hard operations
already retain the first owner within a 1e-6 world-distance band. Smooth union
and subtraction now use that same ownership rule on CPU and GPU. Distance and
continuous material interpolation are unchanged. Validation still requires
exact part agreement and its original distance/material tolerances; error
messages now identify the disagreeing parts and numerical errors.

Focused CPU tests cover both sides of the smooth-union and smooth-subtraction
tie band, plus a point clearly outside it. The previously failing native horn
fixture passes after the fix. All six selected authored/recipe/build/layer
checks pass. `outputs/sdf/refinement-foundation/` retains the complete native
authored regression run, including primitives, tapered shapes, smooth/cut
materials, lifecycle, maximum capacity, legacy scenes and export/reload.

## Progress

- [x] Preserve original inputs and compare curve treatments.
- [x] Fix and qualify the exposed smooth-operation ownership instability.
- [x] Finish guardian native refinement and intent checks.
- [x] Finish driftwood native refinement and intent checks.
- [x] Qualify both construction histories and publish matched comparisons.

Artistic assessment and user acceptance are recorded separately from the
native correctness results.

## Modeling checkpoints

The opt-in `author_refinements.py` emits separate `sculpture-*-refined` scenes
and static/build presets. All original blockouts and finished scenes, plus
the two refinements, reproduce byte-identically; the receipt is
`outputs/sdf/refinement-reproduction.json`. No original scene/preset changed.

`outputs/sdf/refinement-audition-final/` retains two clay form variants per
subject, with front, hero, profile and rear views and exact source inputs.
The restrained guardian cheek treatment (A) retains more quiet face area and
is selected. The compact wood fork (A) separates its rear limb more clearly
than the leaning treatment (B). Earlier native revisions remain under
`outputs/sdf/refinement-audition/` and `refinement-detail-{1,2}/`.

The guardian now has resolved socket rims, cheekbones, muzzle/lip and nose
wings, raised brow folds, inner ears, longitudinal horn flutes, neck/shoulder
structure and a few chips/cracks. The correction pass restores readable
nostrils after a first placement was too deeply buried. It uses 108 leaves /
215 instructions. Seven saved-volume connection/socket/chip checks pass.

Driftwood uses fewer dominant limbs, a rearward turn, oblique broken ends,
broader worn planes, an elongated knot, two hollows, a real longitudinal split
and grain extending to the side/rear. The correction pass trims the rear limb
and shortens a rear scoop that cut too close to that junction. Its retained
rear-limb probe now has over 0.021 workspace units of interior clearance.
It uses 100 leaves / 199 instructions. Twelve design checks pass, including
solid-before/empty-after samples at the hollow and split. The split's front
wall sample changes from -0.002384 to +0.009548 signed distance.

`outputs/sdf/refinement-preflight/` records successful final native ray and
CPU/GPU distance/material/part checks. These preflight captures alone did not
establish construction acceptance; the subsequent full history matrices,
performance and films are recorded below.

## Construction visibility correction

The first full guardian capture passes all native prefix checks but stops at
the preview-visibility gate. Its retained attempt is
`outputs/sdf/refinement-guardian-preview-attempt/`. Native on/off comparisons
identify the far ear and eight rear-neck segments as fully occluded from the
front build camera. These have individual reasons in
`sculpture_refinements.json`; opposite/profile/rear inspection remains required.
The analogous driftwood preflight finds all four side-grain previews visible,
while six rear-grain segments are occluded and explicitly documented.

The same check reveals that the original jaw-chip cutter was buried. It is
moved onto the exposed jaw edge rather than exempted. An independent sample
changes from -0.005944 before that cut to +0.009720 afterward, with a continuous
empty path to the exterior. The revised native cutter preview is visible.
These corrections retain the visibility threshold and original ray/material
tolerances. The full guardian capture is rerun against the corrected scene.

That rerun finds one final minor exception: the tapered temple-crack endpoint
changes 69 color channels at the fixed build camera, below the unchanged
100-channel visibility gate. Its native face-close-up changes 164 channels
and passes the same gate; this supplemental on/off comparison is required by
the subject metadata. The second attempt is retained at
`outputs/sdf/refinement-guardian-small-tip-attempt/`.

Driftwood's five initial split segments have visible previews. The sixth
extends the void inside the trunk behind the front wall and is documented
individually. Its sample changes from -0.011887 before that segment to an open
interior, with a retained empty path into the visible split. The upper hollow
also has an explicit same-point solid-before/empty-after check.

The history qualifier can now reuse prior stills/native checks only when the
scene, preset, complete history/timing, binary and shader hashes match. It
additionally verifies the command, PNG hash and native log statistics for each
reused run. The new receipt identifies the original source and capture
manifest. Video and export side effects run again. Different-scene and corrupt
image negative controls both reject reuse before native execution, recorded in
`outputs/sdf/refinement-reuse-negative-controls.json`. This avoids recapturing
identical geometry solely to revise visibility metadata; it does not waive any
native oracle or preview requirement.

## Legacy robot image boundary

The matched comparison initially demands exact old/new robot pixels and stops:
the smooth-operation ownership fix changes two channels of one pixel at
(411, 508), from G/B = 62/65 to 64/69. Repeated current captures are identical.
The robot scene/preset remain byte unchanged. Current native field checks pass
with zero mismatches/exhaustion and the same five finite-grid ambiguities
recorded in the earlier joint-review hero/reload/atlas logs. Direct CPU/GPU
evaluation passes 1,303 points with zero material/part disagreement.

The comparison now records this bounded difference under the already
established image gate (maximum channel error 16, mean 0.002, fraction above
one of 0.0001), rather than claiming exact legacy pixels. Observed maximum is
4, mean is 0.00000217, and two channels exceed one. These are the existing
history-image limits, not enlarged thresholds selected for this result. The
matched capture receipt preserves exact metrics, commands and old/new RGB
hashes. New sculpture static/replay and export/reload identity remain separate
checks. A finish-existing option verifies every saved image and exact
scene/config/native hashes before completing this comparison, without rerunning
the already completed baseline profiles.

## Final native qualification

The final receipts are `outputs/sdf/refinement-{guardian,driftwood}-qualified/`.
Both qualification starts have clean source states. Binary and all thirteen
shader hashes match the shared regression, controlled curve comparison,
matched before/after captures and private desktop-control runs.

| Subject | Qualification source | Distinct states | Native history checks including revisits | Rays | CPU/GPU points |
| --- | --- | ---: | ---: | ---: | ---: |
| Guardian | `9a278156` | 109 | 112 | 53,898 | 126,954 |
| Driftwood | `08cd460d` | 101 | 104 | 48,418 | 115,074 |

The guardian reuses 202 verified still/check captures from the corrected
`26745a24` attempt, with identical scene/preset/binary/shader/device identities.
Its final export, video, inspection views, atlas relocation, supplemental crack
preview and profiles run again. The receipt records reuse rather than claiming
that every still was newly rendered. Driftwood's matrix is captured afresh.

Together these cover 210 distinct states and 216 history checks, 102,316 rays
and 242,028 direct points. Both subject matrices report zero mismatches,
exhausted rays, material/part disagreements and finite-grid ambiguities. This
zero-ambiguity result belongs to these sculptures; the legacy robot's five
previously recorded ambiguities remain separately reported above.

Final replay/static images are pixel identical for both subjects. Export/reload
preserves the explicit scene and hero pixels, and axis-80 relocation preserves
the hero pixels. Material-only cache checks pass. Each fresh film decodes all
1,500 frames without visible traversal-exhaustion pixels, visits every edit,
and builds exactly the edit count plus one fields (109 and 101).

All 108 cutter on/off pairs retain field/appearance fingerprints. Ninety-one
pass the fixed-camera visibility gate; sixteen are occluded rear/interior
segments, and the one small crack tip passes its required face-close-up gate.
Every exception has an individual reason in the saved metadata. The seven
guardian and twelve driftwood intent checks pass, including the explicit
solid-before/empty-after jaw, hollow and split probes.

`outputs/sdf/refinement-controls/{subject}/windowed-interaction.json` records
passing private-display resize, orbit/zoom, next/previous, empty/final/midpoint
scrubbing, stable paused cache, restart, playback and Escape checks for both.
The six selected authored/recipe/shader/layer/policy checks and all 41 shared
native regression cases pass. Reuse negative controls additionally reject a
wrong preset/scene, corrupt cached PNG and wrong GPU identity before execution.

### Measured cost

RTX 5070 Ti / driver 610.57.04, 1920×1080 at 1× with hard shadows. Each GPU
profile has 148 completed samples after warmup. Matched first-pass profiles
use the same current binary/settings; native capture jobs and profiles run
sequentially. Geometry builds and readback/encoding remain separate costs.

| Subject | Leaves / instructions | Cold build | GPU median / p95, first pass → refined | Final / peak active bricks |
| --- | ---: | ---: | --- | ---: |
| Guardian | 108 / 215 | 98.216 ms | 2.237 / 2.315 → 2.344 / 2.476 ms | 108,036 / 108,036 |
| Driftwood | 100 / 199 | 63.868 ms | 1.962 / 2.044 → 1.939 / 2.016 ms | 75,210 / 75,210 |

Every scene retains the 912,882,864-byte axis-64 allocation and 262,144-brick
capacity. No geometry, instruction, stack, tree-depth or allocation ceiling is
raised. These are content-specific measurements; the small driftwood timing
difference is not a renderer-optimization claim. Construction remains a series
of synchronous rebuilds, not a freehand interaction-latency result.

## Visual assessment and delivery

The guardian is an incremental refinement of the successful design: the
expression and outline remain recognizable, with stronger facial transitions,
carved ears/horns and a little exposed stone damage. The rear skull is still
quite simple and sampled horn transitions remain visible. The driftwood makes
the larger change: broken unequal limbs, a hollow/split and the elongated knot
give it a clearer wood identity. It remains a stylized carving with sparse,
sometimes angular grain, regular cross-sections and visible curve bands under
rear light. These are the assistant's assessments, not user artistic acceptance.

The final browser review loads all 210 edit images, all 28 matched
material/clay view pairs (56 images), both films and all twelve chapters.
Chapter captions are checked after seek completion. Previous/next and keyboard
boundaries, selected-edit seeking, playback/pause and subject-switch reset
behavior pass. The 390px viewport has no horizontal overflow and retains the
film aspect ratio; its temporary viewport override is reset. No browser
warnings/errors are reported. The published URL is checked again, including
the driftwood split chapter with its real red cutter preview.

The report export contains 1,411 files / 98,264,847 bytes, below the publisher's
100 MiB limit. Primary views, edit states and films stay directly accessible;
only auxiliary preview/desktop PNG duplicates are losslessly archived and
hash-verified. HTTP checks reproduce the served HTML, review screenshot,
driftwood film and a clay image byte for byte. The shared report server remains
running; the temporary preview server is stopped after delivery.

Local receipts:

- `outputs/sdf/refinement-study-summary.json`
- `outputs/sdf/refinement-reproduction.json`
- `outputs/sdf/refinement-comparison/manifest.json`
- `outputs/sdf/refinement-browser-review.json`
- `outputs/sdf/refinement-report-delivery.json`
- `outputs/sdf/refinement-served-assets.json`

The immutable published bundle retains the pre-publication browser receipt;
the local receipt additionally records its published-URL check. Source,
explicit scenes, presets and this evidence note are committed. Generated media
remains in outputs and the published snapshot. The original sculptures and
accepted robot remain available; Git push is outside this goal's authorization.
