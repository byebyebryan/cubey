# SDF

SDF rendering and authored-content experiment derived from projectR. Procedural
rounded-box edits and named primitive CSG scenes share sparse distance bricks,
mapless self-shadows and Cubey's atmosphere/HDR presentation. The original
[implementation plan](../../docs/notes/sdf-projectr-port-plan.md) defines the
checkpoints; the [source review](../../docs/notes/projectr-architecture-review.md)
and [development history](../../docs/notes/projectr-development-history.md)
preserve the reference and its experiments.

The guardian sculpture is now settled as an authored SDF rendering/replay demo:
the organic mirrored sculpt, limestone and subtle Balanced marble are retained.
The [guardian closeout](../../docs/notes/sdf-guardian-closeout.md) records the
accepted appearance, review, technical limits and evidence map. Further model
and material realism work is deferred. The full construction film retains its
earlier marble appearance; current finished views and turntables show the
accepted material.

The first authored piece is a small survey robot: 43 shapes, grouped local cuts,
a tilted head and raised arm, with ivory, teal, dark rubber and amber materials.
[Watch the native turntable and inspect its views](http://starship:8787/reports/eafb4462b9072db9f445b21e5d9402466d78e11fd70e23fdcba7d2bf263c8540/).
Run `--config projects/sdf/presets/survey-robot.json`. The
[authored evidence](../../docs/notes/sdf-authored-content-evidence.md) records
the blockout audition, native inspection views, qualification and measurements.
The user accepted the authored robot experiment after the later detail and
joint revisions described below. Each linked capture retains its original scope.

The [edit-by-edit presentation](http://starship:8787/reports/5d6bfde3c2493278b5c68e1ddce68381942ff9d8ef60672a553aa4b6a52f0c7f/)
builds the same robot from an empty workspace through 43 named additions and
cuts. Run `--config projects/sdf/presets/survey-robot-build.json` for a steady
construction view followed by the finished orbit. Previous/next buttons and
the Edit scrubber inspect each committed state; red cut previews precede the
six local removals. The static robot preset above remains available. See the
[history evidence](../../docs/notes/sdf-authored-history-evidence.md).

The separate [mechanical detail study](http://starship:8787/reports/8344b13c50933132bbf4db4a064de66a9f1197a9ff74fa8fdad91368966d5232/)
adds open grippers, a recessed status gauge, head/joint fasteners, toe bumper
seams and backpack vents. Its 61 shapes and 121 instructions fit the existing
limits and retain the four-material palette. The original 43-shape scene and
generator output remain intact.

The current detail scene removes the original ivory `sensor-glint` sphere from
the lens and its build history, letting scene lighting supply highlights. The
linked mechanical-study capture predates this small visual cleanup;
[compare the lens before and after](http://starship:8787/reports/0a6102e62987f7754d93f4baf74fd2a4cdefe6d29d9a2ff7994d8c3c7c2b14c9/)
and see the [follow-up evidence](../../docs/notes/sdf-robot-detail-evidence.md#lens-cleanup-follow-up).

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-detail.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-detail-build.json
python3 projects/sdf/tools/author_robot.py --detailed
```

The detail build takes 44 logical seconds followed by a ten-second orbit.
Matched original/detail captures include close inspections and an extended
edit history. The [detail evidence](../../docs/notes/sdf-robot-detail-evidence.md)
records a sampler precision fix found while checking replay, and its cost.

The [field-kit variant](http://starship:8787/reports/52b40ce5c726673b029f0f22ec2dd21d50d9a297e938beb2c161fe8deec734de/)
continues the modeling with a rolled optic lip and
seal, neck collars, tapered forearms and shins, wrist/ankle seals, elbow bearing
caps, a recessed abdominal service door and two backpack power pods. Its 82
shapes and 163 instructions use new torus, capped-cone and smooth-subtraction
contracts, within a bounded 1,024-shape / 2,048-instruction budget. Both earlier
robot scenes remain intact, including the cleaned lens in the detail model.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-fieldkit-build.json
python3 projects/sdf/tools/author_robot_fieldkit.py
```

The field-kit build uses 50 seconds of construction followed by a twelve-second
orbit. See the [field-kit evidence and vocabulary decisions](../../docs/notes/sdf-robot-fieldkit-evidence.md).

The current field kit derives shoulder/elbow frames from the arm pivots and
places each leg on a shared hip-to-ankle centerline. Neck, wrist, gripper and
pod-collar frames are audited as well. See the
[joint review](../../docs/notes/sdf-robot-joint-review.md) for the placement errors
found after the initial modeling pass;
[compare the revised elbows and front/profile leg views](http://starship:8787/reports/e05293386c1ae7bfeac7985541938ef1bc974b737f381d64ff09044422c99702/).

```sh
python3 projects/sdf/tools/check_robot_joints.py
```

`qualify_robot_detail.py --joint-views` runs this audit before native
qualification and adds front/profile leg and opposite elbow inspection pairs.

The [settled procedural demo](http://starship:8787/reports/1db7103651a39de3b013aabf8424bbcb06e0a236627b8af3c32dcb9306dc6aa1/)
uses width 0.024 and 4,096 cuts, with a 2,048-cut comparison. Four long bores
open the presentation before the dense shorter cuts; a slightly oblique
opening view exposes the first hole's inner wall. Run
`--config projects/sdf/presets/negative-stick-ball-dense.json`, or add
`--count 2048`. This settles the procedural tech demo that preceded the authored
robot. See the
[closeout note](../../docs/notes/sdf-procedural-prototype-closeout.md).

The earlier [dense carving study](http://starship:8787/reports/3a21ce65daedf473f4596ca205421d3d2119e20f73ecb8d79388442b9baae090/)
retains the full geometry qualification and cutter-mode comparison.
`--cutter-mode original|intro-bores|through` compares the treatments; the desktop
mode control applies with Generate scene. See the
[dense-bore evidence](../../docs/notes/sdf-dense-bore-evidence.md).

The [authored content plan](../../docs/notes/sdf-authored-content-plan.md)
sets the small primitive vocabulary and plain-material scope. The robot uses
no procedural patterns, image textures or UV mapping.

The [native sculpture gallery](http://starship:8787/reports/b155234e48461677cf2e6d7bd69569f4f6fc3a79c87ac94e430ad4af1980ed08/)
adds a guardian bust, pierced ceramic vessel, carved rock arch and twisted
driftwood. They explore fused forms, hollowing, real passage breakthroughs and
curved tapered strokes, with plain materials and independent construction
histories. Each has a 40-second construction view and ten-second final orbit.
All 235 committed-state images and the four films are available in the gallery.
See the [study evidence and visual verdicts](../../docs/notes/sdf-sculpture-study-evidence.md)
and its [execution plan](../../docs/notes/sdf-sculpture-study-plan.md). These are
delivered candidates for review; the arch/driftwood remain stylized, and some
sampled stroke transitions are visible under inspection lighting.
The user ranks the guardian strongest and driftwood weakest. A
[refinement pass](../../docs/notes/sdf-guardian-driftwood-refinement-plan.md)
developed both, but user review still found driftwood unsuccessful. It is
retained as an experiment and removed from the active refinement presentation;
further wood detailing is deferred. The guardian remains the selected sculpture.
Its arbitrary jaw/base chips have been removed, retaining the facial carving,
inner ears, horn flutes and neck structure. The
[review and native evidence](../../docs/notes/sdf-guardian-driftwood-refinement-evidence.md)
distinguish this correction from the historical 108-leaf version.
The [corrected guardian review](http://starship:8787/reports/03f1f69693d68ebc71535c764fec08fcdf6763228fa37da7fc11639e393fe2a4/)
shows matched before/after views, all 107 revised states and a fresh construction
film without those two cuts.
The earlier [matched refinement gallery](http://starship:8787/reports/d66a4ee7ce92dd2f255e170b9dc67676551a1f5efec099ec99ccda304a4d7973/)
retains both prior versions, their 210 states and two films as the review record.

The [organic guardian study](../../docs/notes/sdf-guardian-organic-evidence.md)
adds a separate curved-jaw, elongated-skull variant with anatomical relief.
It uses 1,011 leaf reveals across 113 coherent strokes and primitive placements;
its retained form stage has 197 leaves. Authored capacity is now 1,024 leaves /
2,048 instructions, with the existing field resolution and other ceilings.
New strokes can offset along the local surface normal, while legacy authoring
remains reproducible. Use `sculpture-guardian-organic[-build].json`; the earlier
106-leaf refined preset remains available for comparison.
The [completed organic guardian gallery](http://starship:8787/reports/1f385381082f8cb914e5deb57eec6cd42dd17508f7369cb69f250b36bb370931/)
includes a short orbit, the full 1,011-reveal film and selected stills; all 1,012
prefixes pass independent native field checks. At the tested 1080p settings,
the detailed scene costs about 11 ms per frame and 0.9 seconds per full rebuild.
Live playback can skip reveals while rebuilding; the offline film visits each.

The [mirrored stroke study](../../docs/notes/sdf-mirror-evidence.md) adds an
opt-in X reflection helper and atomic whole-stroke history. Its separate
`sculpture-guardian-mirrored[-build].json` variant has 65 operations: 48 mirrored
pairs and 17 single strokes/placements, retaining 1,011 explicit primitives.
The original has 113 stroke placements represented by 1,011 segment reveals.
Central forms retain their asymmetry; bilateral features reflect the original
left stroke onto the right, including the formerly unequal horn tips.
All 66 native states and paired horn/socket desktop undo/redo checks pass.
Matched 1080p profiles measure 10.73 / 10.67 ms for original / mirrored scenes;
both still take about 0.92 seconds to rebuild. The reduction is in authoring and
replay actions, not steady evaluation cost. This is scripted authoring, not a
new live brush editor.
The [mirrored guardian review](http://starship:8787/reports/28b97723efe9c303766c0ad148b500cfc49758af39d488a292e616d0ce512216/)
includes the construction film, all operation stills, paired red previews,
matched inspection views and measurements.

The [procedural stone audition](../../docs/notes/sdf-procedural-materials-evidence.md)
adds limestone and honed-marble variants of that same mirrored guardian.
Seeded color, roughness and filtered fine-grain bump share a continuous model-space
volume across the sculpt and its cuts. Geometry and all 65 operations remain
identical; appearance edits retain the field cache. The
[material comparison](http://starship:8787/reports/65b3ef76be120d14f259412977d3e50fa6e59e435298cd415ece6edb84d1ec87/)
includes twelve matched views per material, three turntables, inspection passes
and the complete marble construction film. Limestone reads more convincingly
in native inspection; marble remains a stylized surface treatment.
User review accepted the limestone appearance and requested finer marble.
The denser trials then proved too busy in user review. The current marble
preset adds a modest amount of detail to the original: scale 6, vein frequency
7.25 and gentler warping, retaining its pale colors and restrained bump.
The [balanced comparison](http://starship:8787/reports/f59ca1bd61e2982b7a4d83a45fd0dc4c821e33078218a3cbf52cbbe1aadec403/)
places it between the original and previous denser trial; the
[follow-up evidence](../../docs/notes/sdf-procedural-materials-evidence.md#balanced-marble-follow-up)
records the selection and native checks.
User review accepted Balanced. It is now the default in the scripted generator
and the guardian's native material panel, with Original, Denser and Fine also
available. An independent body-color control adds cream mineral variation to
the original pale material; the saved guardian uses 0.08, and zero retains the
previous material. The initial tint was too faint; later user review retained
cream but rejected the bluish cool-gray end. The
[original and cream review](http://starship:8787/reports/de9be4cc6f48d9b5723fb4e0f6378d222bce6ae528c5844b3dbdb8927a1f136b/)
compares original color, default and stronger cream variation, including a
matched 0.16 comparison with the previous cool endpoint.
User review then found the soft placement stain-like. The
[mineral-pocket review](http://starship:8787/reports/2fe7ca71500f4f1a4318deafb4429b80d10cb192f38a5776a1e1414520b4eb96/)
compares the broad tint with smaller, more defined cream pockets that favor
the warped vein bands. It includes natural-marble references, a same-amount
0.16 pair, neutral/default/stronger views and native turntables. A later
follow-up connected the color directly to the marbling: warm vein cores,
irregular cream shoulders, attached pockets and colored secondary wisps.
The [coherent-color plan](../../docs/notes/sdf-marble-minerals-plan.md) defines
the boundary; the
[native review](http://starship:8787/reports/0feff6f5a8207ee1f3ed7b6933cbb434b55ff191e8365f248e414699972978ec/)
pairs both revisions at equal amounts and adds an unlit face close-up.
User review found that placement too strictly tied to veins. The
[partially correlated region study](../../docs/notes/sdf-marble-regions-plan.md)
gives cream regions their own boundaries and fine internal streaks, sharing
the larger deformation with the marbling. Veins can cross pale and cream
regions, and selected warm seams remain. The
[current native review](http://starship:8787/reports/0a7461860a17aeccae6adc3f5e16cdafa358f9d28031cbd5b6fa3299ceb2a1ca/)
starts with matched flat SDF swatches before the guardian views and turntables.
Default 0.08 remains restrained. The smooth, closed vein topology remains a
visible limitation. User review accepts the current result as stylized honed
marble and settles further color/vein refinement; see the
[closeout](../../docs/notes/sdf-guardian-closeout.md).

A separately qualified shortcut now skips the CSG material-ownership walk for
one-material shaded hits. The original three material-audition variants measure
about 1.8 ms at the tested 1080p settings, superseding the earlier shading costs
above. General multimaterial and part-ID queries still use the full evaluator.
Geometry rebuilds remain about 0.92 seconds. The evidence separates shortcut,
pattern-only and bump costs; material differences are within timing variation.
Later material revisions retain their own appearance/history qualification
scopes; those earlier timings keep their original material snapshots. A matched
1080p pair before the color-visibility correction measured 1.76 ms with tint off
and 1.78 ms at 0.08; geometry rebuilds remained about 0.92 seconds. That profile
retains its earlier shader identity; the palette correction has no new profile.
The current mineral-region revision has a fresh matched tint-off/on pair:
1.781 / 1.799 ms median at 1080p, with about 0.92-second cold field builds.
The stronger 0.16 amount measures 1.795 ms. These measure cases within the
revised build; previous shader timings retain their earlier receipt scope.

| Sculpture | Static preset | Construction/orbit preset | Leaves |
| --- | --- | --- | ---: |
| Guardian | [Static](presets/sculpture-guardian.json) | [Build](presets/sculpture-guardian-build.json) | 76 |
| Ceramic vessel | [Static](presets/sculpture-vessel.json) | [Build](presets/sculpture-vessel-build.json) | 10 |
| Rock arch | [Static](presets/sculpture-arch.json) | [Build](presets/sculpture-arch-build.json) | 51 |
| Driftwood (retained experiment) | [Static](presets/sculpture-driftwood.json) | [Build](presets/sculpture-driftwood-build.json) | 94 |
| Guardian, refined / chips removed | [Static](presets/sculpture-guardian-refined.json) | [Build](presets/sculpture-guardian-refined-build.json) | 106 |
| Guardian, organic form | [Static](presets/sculpture-guardian-organic-form.json) | [Build](presets/sculpture-guardian-organic-form-build.json) | 197 |
| Guardian, organic detail | [Static](presets/sculpture-guardian-organic.json) | [Build](presets/sculpture-guardian-organic-build.json) | 1,011 |
| Guardian, mirrored strokes / 65 operations | [Static](presets/sculpture-guardian-mirrored.json) | [Build](presets/sculpture-guardian-mirrored-build.json) | 1,011 |
| Guardian, procedural limestone | [Static](presets/sculpture-guardian-limestone.json) | [Build](presets/sculpture-guardian-limestone-build.json) | 1,011 |
| Guardian, honed marble | [Static](presets/sculpture-guardian-marble.json) | [Build](presets/sculpture-guardian-marble-build.json) | 1,011 |
| Driftwood, refined (retained experiment) | [Static](presets/sculpture-driftwood-refined.json) | [Build](presets/sculpture-driftwood-refined-build.json) | 100 |

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-build.json
python3 projects/sdf/tools/author_sculptures.py --stage final
python3 projects/sdf/tools/qualify_sculpture.py --subject guardian --video \
  --output outputs/sdf/sculpture-guardian-new
python3 projects/sdf/tools/author_refinements.py --subject guardian
python3 projects/sdf/tools/qualify_sculpture.py --subject guardian \
  --metadata projects/sdf/sculpture_refinements.json --video \
  --output outputs/sdf/guardian-refined-new
```

`sculptures.json` defines per-subject inspection views and solid/empty design
probes. The authoring helper samples curved paths into rounded tapered segments
and projects selected grooves onto the actual surface. It emits explicit scenes
without an embedded Python dependency. This is scripted CSG authoring and
leaf-reveal or grouped stroke playback; a freehand brush editor and exact spline
sweep remain separate work. Atmosphere, hard self-shadows and the absence of a ground plane
carry over from the robot/procedural studies.

The refinement helper adds monotone radius interpolation and projection from
selected world-axis sides, still exporting existing rounded segments and CSG.
It preserves first-pass generation. `qualify_history.py --reuse-captures` (or
`qualify_sculpture.py --reuse-history`) can reuse verified stills/native checks
only with matching scene, preset, history/timing, binary/shader and GPU identity.
Each reused run retains its original command/provenance and verifies image/log
evidence; films and export side effects run again. Selected minor fixed-camera
preview exceptions have individual reasons and, where needed, required native
close-up comparisons. The established oracle/image thresholds remain unchanged.

The [negative-sticks experiment](../../docs/notes/sdf-negative-sticks-plan.md)
starts from a solid and carves intersecting through-holes with a deterministic
sequence of thick box cutters. It includes a native seed audition, slow cutter
guides, accelerated carving and a final orbit. See its
[qualification evidence](../../docs/notes/sdf-negative-sticks-evidence.md).

The [sphere comparison](http://starship:8787/reports/597b5db0fb6edef3a52ec569083ae0719b10b39a3104102eb719a0da9c5fadf9/)
tries the identical cutters against a radius-0.36 ball. Run it with
`--config projects/sdf/presets/negative-sphere.json`; the original rounded-cube
preset remains available. See the [sphere evidence](../../docs/notes/sdf-negative-sphere-evidence.md).

The [denser sphere study](http://starship:8787/reports/f23de4a4ab24f55ab4403a145e1b4fe87a0548c6abf279fbe51561eda92cc096/)
compares up to 2,048 cuts and includes a fresh 256-cut carving video. Run
`--config projects/sdf/presets/negative-sphere-dense.json` for that candidate.
At higher counts the broad cutters mostly erase the ball; a quarter-width
trial instead reaches the field capacity at 256 cuts. See the
[density evidence](../../docs/notes/sdf-negative-sphere-density-evidence.md).

For the additive ball's actual slender geometry,
[watch the matched comparison and carving video](http://starship:8787/reports/0254a0153dd18172f3e8ff11cb667a5b6219c4cf11a41f11c48121c817866851/) or run
`--config projects/sdf/presets/negative-stick-ball.json`. This subtracts the
exact original 2,048 sticks from a radius-0.30 sphere: full width 0.003 and
length 0.101–0.401, including rounding. The result is a fine pattern of slots
and punctures. Both 2,048 and 4,096 cutters fit the existing axis-92 profile.
See the [thin-stick evidence](../../docs/notes/sdf-thin-negative-sticks-evidence.md).

The [intermediate-width comparison](http://starship:8787/reports/32adae0557e41e2a1b5d3d014b07ea67c4225c71dd33c9862dc740d9cbfcbcca/)
balances visible cuts with an intact ball. Its candidate preset is
`--config projects/sdf/presets/negative-stick-ball-balanced.json`: 512 cutters,
full width 0.015. `--cutter-width` and the desktop width control tune this
generator from 0.003 to 0.06; Generate scene applies staged changes. The
report compares 16 width/count combinations under identical conditions.
See the [width evidence](../../docs/notes/sdf-intermediate-width-evidence.md).

[Watch the carving video and compare the seed audition](http://starship:8787/reports/3d8baa7ee96f132b28062ace6e4878538982df72c66aea7c0094f884b2729f65/).

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-sticks.json
```

[Watch the earlier additive demo and review its captures](http://starship:8787/reports/ac8d841ecc3900e08a9eaa6cced9597a0d466c4ad511142376a65adfffe8f31b/).
The review includes the 24-second presentation, lighting comparisons, explicit
recipe, applied configurations and qualification receipts. The app and current
presentation preset use hard self-shadows against the shared atmosphere sky,
with the ground plane disabled. The report links the earlier ground-plane
study, whose downward camera and receiver hid the sky.

The native renderer builds a four-stage hierarchy at depths 2/4/6/8 and samples
8-cubed R16F bricks. The procedural builder filters ordered box edits into leaf
lists and promotes completed batches of at most 128 edits. The authored builder
classifies and samples complete grouped expressions; geometry changes currently
rebuild the entire field. Both publish a replacement only after successful
construction. Complete CPU scene descriptions remain independent of those
caches. Empty cells use conservative box exits; hits write surface depth, and
exhaustion is visible in magenta.
Queries interpolate the eight neighboring R16F texels with brick-local weights.
This preserves the sampled field while avoiding interpolation differences from
normalized atlas coordinates when identical bricks occupy different addresses.

The scene uses Cubey's render graph, shared HDR post, atmosphere sky integration
and atmosphere-derived direct light/diffuse irradiance. Construction, orbit and
the diagnostic presentation use a deterministic logical timeline. Native V1
checks and the fixed performance matrix pass on the selected RTX 5070 Ti;
the captured composition is ready for user visual review.
See the [evidence record](../../docs/notes/sdf-implementation-evidence.md).

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_sdf cubey_project_sdf_recipe_tests cubey_project_sdf_authored_tests
ctest --preset dev -R '^sdf_(recipe|authored)_tests$'
build/dev/projects/sdf/sdf
build/dev/projects/sdf/sdf --headless --scene cut --debug 1 --no-ground \
  --validate-field --require-validation --output /tmp/sdf-cut.png
build/dev/projects/sdf/sdf --headless --count 2048 --atlas-axis 92 \
  --shadows 1 --output /tmp/sdf-sticks.png
```

Drag to orbit, scroll to zoom, R to reset, Space to pause playback and
Escape to close. Static orbit/view changes reuse the existing field.
`--scene` selects `box`, `rotated`, `cut`, `sticks`, `negative-sticks`, `negative-sphere`
or `negative-stick-ball`;
`--seed` and `--count` specify the reproducible recipe. For negative scenes,
count means cutters, excluding the one initial Fill (zero cutters retains the
solid; maximum 65,535). The original `sticks` generator is unchanged.
`--yaw`, `--pitch` and `--distance`
specify the capture camera in radians/workspace units. `--debug` selects shaded
(0), normal (1), ray-distance (2), traversal-work (3) or brick-cell (4) display.
Authored scenes additionally expose part identity (5), base color (6) and
roughness (7).
Hard mapless self-shadows are the default (`--shadows 1`); `--shadows 0` disables
them. The ground plane is off by default; `--ground` enables it for diagnostics.
`--shadows 2` explicitly selects
four-sample soft shadows, with angular radius controlled by `--shadow-softness`.
`--ao` enables four local hemisphere queries affecting
environment visibility. Neither mode provides indirect bounced light.

`--sun-elevation` and `--sun-azimuth` use degrees; `--exposure` uses stops.
`--roughness` adjusts the legacy plain material or the `--clay` inspection
override. Authored materials carry their own roughness. `--supersample 2` renders at twice
the output width/height and filters through the shared post pass, useful for
thin-feature captures. Default sampling is 1 for timing runs. The sky uses the
foundation's backdrop horizon remapping; this does not remap surface lighting.

`--occluder` draws a raster plane crossing the sculpture to check surface depth.
`--validate-field` compares GPU rays against direct CPU CSG, including axis
parallel and inside-solid rays. Authored scenes also compare direct GPU distance,
material and part evaluation with the CPU and check material-only cache reuse.
`--validate-batches` checks empty/reset and 128/129/256/257-edit scenes with an
individually visible box per edit. In authored mode it additionally checks failed
build rollback, recoloring and transitions between scene formats.

`--export-recipe /path/scene.json` saves every ordered edit, including the final
partial batch. `--recipe /path/scene.json` rebuilds from that explicit list.
The versioned file stores forward quaternions as xyzw, generator metadata and
a geometry hash; invalid geometry, unsupported versions and hash mismatches
fail before upload. The generator seed alone is not the saved sculpture.

## Authored scenes

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot.json \
  --headless --validate-field --require-validation --output /tmp/sdf-robot.png
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot.json \
  --export-authored /tmp/survey-robot.json
python3 projects/sdf/tools/author_robot.py
```

`--authored-scene PATH` loads the separate version-2 `cubey.sdf.scene` format.
The [explicit robot scene](scenes/survey-robot.json) and its
[authoring helper](tools/sdf_author.py) demonstrate the contract. Loading the JSON
does not require Python. `--export-authored PATH` saves the named tree with the
current material values and separate geometry/appearance fingerprints. The
desktop Export button uses that path; Reload reads the original input file.
Hashes are validated when present, so handwritten modifications to an exported
file must remove its old fingerprints or supply recomputed values.

Named nodes support box, sphere, Y-axis capsule, Y-axis capped cylinder, torus,
capped cone, rounded tapered segment (`round-cone`), union, subtraction,
polynomial smooth union and smooth subtraction.
Groups inherit material assignments
and apply local position, forward `rotation_xyzw` and positive uniform scale.
Boxes use core `half_extent` plus `rounding`; spheres use `radius`; capsules and
cylinders use `radius` and `half_length`. A torus revolves about local Y, using
positive `major_radius` and `minor_radius`, with minor strictly below major.
A capped cone uses `bottom_radius` at local Y = `-half_length` and `top_radius`
at `+half_length`; radii are nonnegative with at least one positive. Equal radii
give a cylinder, and a zero end radius gives a pointed cone. Torus and cone
support the same inherited rotation, position and uniform scale as other nodes.
`round-cone` uses the same endpoint-radius and half-length parameters as a
capped cone, but describes the convex hull of spheres at Y = ±half-length.
Equal radii give a capsule, and contained endpoints reduce to the larger
sphere. See the [rounded stroke contract](../../docs/notes/sdf-rounded-stroke-contract.md).
Union/smooth-union fold two or more children in order; both subtraction forms
have exactly two. Scene limits are 1,024 primitive leaves, 2,048 instructions,
16 materials, tree depth 24 and GPU stack depth 32.
The composite is clipped to the existing workspace box of half-width 0.499.
Unary `group` nodes preserve a single child's local transform/material scope;
`root: null` represents an empty workspace.

Materials store linear RGB `base_color`, scalar `roughness` (0.05–1) and RGB
`emission` (0–32). The authoring helper converts sRGB hex colors to linear RGB.
The desktop material controls expose those linear values. Hard and smooth unions preserve
the earlier branch within a 1e-6 world-distance tie band. Subtraction inherits the
retained body material unless `cut_material` selects newly exposed surfaces.
Smooth unions take a `radius` and `blend_materials` flag; nested blends evaluate
the full material expression. Smooth subtraction also takes a positive `radius`:
it applies a polynomial smooth maximum to the body and negated cutter distances.
It removes up to `radius / 4` more material than a hard subtraction and preserves
the builder's 1-Lipschitz bound. Without `cut_material` the body owns appearance;
with it the cutter-facing half of the blend selects that material and the
operator's part ID. The body owns a tie within the same 1e-6 distance band. Geometry smoothing and cut
appearance remain independent. Smooth cutters use the same preview/history
ordering as hard cuts. These appearance rules do not change geometry.

Per-material edits upload small tables without rebuilding distance bricks.
No appearance volume is allocated. The default atmosphere route uses dielectric
direct lighting and sky diffuse irradiance. The separate material-study route
below adds optical properties and generated specular IBL; GI remains deferred.

### Procedural stone surfaces

Materials may include an optional `procedural` record with `type` set to
`limestone` or `marble`. The bounded parameters are `seed` (integer 0–65535),
`scale` (0.25–64), `color_strength` (0–1), linear `secondary_color`,
`roughness_variation` (0–0.5), `bump_strength` (0–0.005 model units),
`grain_scale` (1–512), `vein_direction` (nonzero vector), `vein_frequency`
(0.25–32), `vein_width` (0.02–0.5), `warp_strength` (0–12), marble
`color_variation` (0–0.25), and fixed model-space `offset`.
Omitted parameters use defaults; plain legacy materials retain their
appearance fingerprints.

Patterns use the scene's fixed model coordinates, continuously across primitive
segments and mirrored sides. They do not normalize against current history
bounds or follow individual moving parts. Subtractions expose the same volume;
explicit cut materials and smooth material blending retain their existing
ownership rules. Patterns are evaluated once per material at a surface query,
not at every primitive or tracing sample. Normal detail blends using the same
material weights. Color, roughness and analytic height gradients use the shared
seeded value-noise family; fine grain fades with a conservative projected pixel
footprint, and marble vein ramps broaden/average when unresolved.

For marble, `color_variation` adds seeded cream mineral regions with their own
boundaries, finer directional interior streaks and edge breakup. They share
the larger deformation with the veins, while their support extends between
the narrow vein paths. Veins cross pale and cream regions; selected warm
cores, shoulders and secondary wisps remain. The original pale/cream
multipliers and amount control retain their values. Analytic gradients account
for the shared deformation and anisotropic scales. Pixel footprints filter
spatial frequencies and broaden narrow color boundaries; unresolved warm
structure fades towards neutral. This affects color only, preserving roughness
and bump. Zero retains the accepted neutral Balanced material exactly, and
remains the default for legacy scenes; the guardian explicitly saves 0.08.
It introduces no separate control, scene field, image map or appearance volume.
This is a stylized treatment with smooth, closed vein paths still visible.

In the expanded body material, scroll to **Marble detail** for Original,
Balanced, Denser or Fine. These choices set pattern scale, vein frequency,
width, warp and grain scale, preserving palette, roughness, tint, orientation,
seed and bump amplitude. Manual edits to those five controls display Custom.
**Body color variation** is independent. Both types of edit retain distance
bricks. To save interactive edits, launch with
`--export-authored outputs/sdf/my-marble.json` and use **Export authored scene**
after editing; the saved explicit values restore the inferred detail choice.

`--no-procedural` shows the material's flat base values, `--no-bump` retains its
color/roughness pattern without normal detail, and `--clay` retains the existing
shape inspection override. Debug 1 remains geometric normals; debug 8 shows
surface shading normals. Shadow/AO traces and ray-origin offsets always use
the geometric normal. Bump affects lighting without changing silhouettes, depth
or distance bricks. Appearance edits retain the geometry cache and persist in
the scene's appearance hash.

One-material shaded scenes can skip the CSG material-ownership walk; part-ID
and general multimaterial queries still use it. `--no-material-fast-path`
exposes the full evaluator for qualification/performance comparison. Procedural
color agreement uses a 0.0002 linear-value bound for thin vein-ramp sensitivity;
plain materials, geometry, height gradients and shading normals retain their
0.00002 direct CPU/GPU bounds. The actual errors are recorded in native logs.

```json
"procedural": {
  "type": "limestone", "seed": 173, "scale": 9,
  "secondary_color": [0.22, 0.18, 0.12],
  "color_strength": 0.65, "roughness_variation": 0.12,
  "bump_strength": 0.00035, "grain_scale": 240
}
```

`author_guardian_materials.py` deterministically emits both material scenes and
their static/build presets from the saved mirrored guardian. Its
`--marble-detail original|balanced|denser|fine` flag defaults to balanced;
`--marble-color-variation` defaults to 0.08 and accepts 0–0.25. These authoring
flags rewrite the generated files. For example:

```sh
python3 projects/sdf/tools/author_guardian_materials.py \
  --marble-detail balanced --marble-color-variation 0.08
```

Use
`--config projects/sdf/presets/sculpture-guardian-limestone.json` or
`--config projects/sdf/presets/sculpture-guardian-marble-build.json` to inspect
them. The [material evidence](../../docs/notes/sdf-procedural-materials-evidence.md)
records native qualification and the implemented
[audition plan](../../docs/notes/sdf-procedural-materials-plan.md).
Authored scenes support static/orbit playback. Scenes with an optional saved
`history` additionally support construction and presentation. Carve continues
to require a legacy edit sequence.

### Sphere and guardian material set

The [bounded material study](../../docs/notes/sdf-material-study-evidence.md)
adds separate `material-study/` presets for a radius-0.25 sphere and the unchanged
guardian. Both use identical material records: neutral gray, accepted limestone and
subtle marble, granular terracotta, ivory ceramic, satin bronze and tinted glass.
The six opaque looks are qualified on both subjects. Homogeneous sphere glass
is qualified; the earlier four-crossing guardian comparison was experimental
because its worst sampled view reached the cap on 1.1068% of visible glass
pixels, above the declared 1% admission limit. That historical result remains
intact. The new eight-crossing qualification is documented below.

The [updated paired material report](http://starship:8787/reports/d7311c3fe7081ab555c02dad8742497d9cc57fb04424c652a350adf95641eb51/index.html)
includes normal/detail views, native orbits, glass diagnostics and separate
shading/build/environment measurements for each material. It also includes the
three-material contrast pass and the clay component diagnosis. The
[first material report](http://starship:8787/reports/f9ff89a12a4569c7f1c934cd5c9996570b316afc9ffcbed89d9931ed016983ca/index.html)
retains the original gray-green, teal and uniform-clay comparison.

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/material-study/sphere-ceramic.json
build/release/projects/sdf/sdf --config projects/sdf/presets/material-study/guardian-bronze.json
```

`--lighting atmosphere` remains the default and retains accepted old images.
`--lighting material-study` selects the fixed shared generated IBL environment,
including its visible background, diffuse response, prefiltered reflections and
DFG lookup. It uses one frozen direct-light setup and no ground/raster-occluder
composition. `material-furnace` is the constant-white diagnostic route, which
flags energy above 1.02 before output clipping. Environment setup is separate
from GPU frame and cold field measurements.

Materials may add an optional `optics` record with `metallic` in [0,1], binary
`transmission`, `ior` in [1,2.5], positive finite `attenuation_distance` and
linear RGB `attenuation_color` in (0,1], plus `diffuse_roughness` in [0,1].
The latter defaults to zero (Lambert) and maps 1 to Oren–Nayar sigma 90 degrees.
Direct diffuse uses Oren–Nayar; environment diffuse uses its conservative A term
with the existing irradiance cube, without full angular integration.
Defaults (0, 0, 1.5, 1, white and zero diffuse roughness)
preserve legacy hashes and exports. Non-default optics require study lighting,
participate in the appearance hash and preserve geometry/field caching. A
parallel 32-byte table leaves the prior material/pattern/geometry layouts intact;
the formerly reserved attenuation W lane stores diffuse roughness.
Opaque blend/cut ownership applies to optics on CPU and GPU.

Glass requires one plain homogeneous material: white base, zero metallic and
emission, fixed roughness 0.05 and no procedural/bump pattern. Geometric normals
control refraction. Measured in-medium distance controls Beer-Lambert tint.
The compatibility default processes at most four interfaces. `--glass-crossings`
accepts 4, 8 or 16; new environment presets explicitly select 8. There is environment fallback for total
internal reflection and crossing-budget limits. This is not recursive optical
transport, rough glass, caustics or transmission through other opaque objects.
`--debug 9` shows crossing-cap/TIR flags and coverage. The earlier report and evidence
separate the qualified sphere from the four-crossing experimental guardian;
the later eight-crossing pass requalifies the sampled budget. A successful
render alone does not establish admission.

The old `--clay` option still produces the same gray inspection material at
roughness 0.36. Its UI label is now **Neutral inspection**. The dry clay catalog
entry now has near-maximal specular roughness, filtered granular bump and
opt-in diffuse roughness 0.6. The [contrast-pass record](../../docs/notes/sdf-material-contrast-pass.md)
explains the old clay diagnosis and the bounded corrective work. Optics are initially authored in
saved scenes/presets; no general optics editor or material graph is added.
`--debug 10` and `--debug 11` inspect opaque diffuse and reflection contributions
under study lighting, with the presentation exposure/tone map. They exclude
emission; the displayed images do not add linearly after tone mapping. Glass
rejects these opaque-only modes. Normals/roughness keep their raw debug display.
The `granular` procedural type reuses the established filtered grain sampler
without the stone-specific name; clay uses zero procedural color strength.

### Shared environment presentation

The implemented [environment presentation plan](../../docs/notes/sdf-environment-presentation-plan.md)
and [native evidence](../../docs/notes/sdf-environment-presentation-evidence.md)
add an opt-in `material-atmosphere` route. It consumes shared sun state,
Lambert-normalized diffuse SH and previous/current prefiltered reflection probes.
Glass samples the same distant environment. The existing material-study presets
retain their fixed environment, and the legacy atmosphere route retains its look.

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/environment-study/guardian-bronze-sunset.json
build/release/projects/sdf/sdf --config projects/sdf/presets/environment-study/guardian-glass-clouds.json
```

The `environment-study/` presets compare fixed reference, clear low sun and
experimental frozen broken cumulus on bronze, glass, marble and terracotta.
The [paired environment report](http://starship:8787/reports/e4177f559cdff0f9f13544c57affe21ef70f6c3db3afe7aad74a64afd582ac1c/index.html)
contains 48 stills, six native orbits and the scoped performance/validation
evidence. All reuse accepted scene/material files and exposure +0.6. The new low-sun
routes use elevation 8 degrees and azimuth -155 degrees. Clear sky is the main
presentation; clouds retain visible horizon/sampling artifacts and are an
integration experiment. No ground plane or terrain is added. Shared distant
planet radiance contributes the lower reflection hemisphere; the separate
visible sky uses the shared backdrop horizon policy.

`--environment-extent 64|128` selects reflection face resolution (default 128).
`--clouds` requires material-atmosphere; `--cloud-coverage` is in [0,1] and
`--cloud-time` is a nonnegative frozen weather time. The presentation uses
coverage 0.28 at 300 seconds. `--no-cloud-backdrop` is a compositor diagnostic
that retains clouded reflections while hiding the visible layer.
Sky/cloud resources settle before frame zero, then reuse their probes while
the camera/material changes. Native sun controls update the environment without
rebuilding distance bricks. The depth-aware HDR cloud composite excludes both
opaque and glass foreground. Diffuse remains clear-sky SH; there are no cloud
shadows on the sculpture.

A fixed-environment 24-angle guardian sweep measured worst cap fractions of
1.2433%, 0.1409% and 0.1258% at 4/8/16 crossings. Eight passes the retained 1%
gate and was confirmed on the final environment binary. The default remains
four for old captures/presets. This qualifies the selected budget on sampled
views; glass still has environment-only exit and TIR approximations, without
local reflections, caustics or more internal reflection bounces.

Reproduce the bounded evidence with `tools/qualify_environment_study.py`,
`tools/qualify_glass_budget.py --budgets 8`, and
`tools/capture_environment_study.py` under this project. Each requires a fresh
`--output` directory. `tools/profile_guardian.py` accepts repeated `--case NAME
PRESET` pairs. New-route timings use `sdf.whole_scene` to include sky/cloud/post;
setup and synchronous field builds remain separate measurements.

### Authoring history

The optional `history` array lists each primitive leaf exactly once in the
intended authoring order. Each entry contains either `node` (one unique primitive
name) or nonempty `nodes` (all primitive names in an atomic stroke),
`label` and an optional positive `seconds` weight (default 0.65, range 0.2–5).
An atomic stroke commits and undoes all its segments together. Names must be
unique across all entries, cover every primitive, and have the same additive or
subtractive polarity within an entry. Legacy single-leaf histories remain valid.
Groups/operators are derived from the final tree. Every revealed leaf must
participate immediately; a cutter before its retained body is rejected while
loading. Missing children reduce to transform groups or an empty root, keeping
the original local coordinates and inherited materials. The final prefix
retains the complete scene and its geometry/appearance fingerprints.

`tools/mirror_author.py` exposes `reflect_x(subtree)` and
`MirrorAuthor(mirror_x=True).edit(subtree, label)`. Build a complete curve/tool
subtree, then register it as one operation. The returned subtree contains the
original and reflected copy. `centerline=True` emits once; `mirror=False` or
changing `author.mirror_x` disables mirroring for asymmetric finishing.
Names must remain unique. Mirroring reflects nested positions and quaternion
frames, retaining taper direction, Boolean structure, scale and materials.
The helper exports ordinary geometry and a `nodes` history entry; its optional
`mirror_axis` annotation is authoring metadata, not a runtime coordinate fold.
Keep each operation's leaves uniformly additive or subtractive.

```python
from mirror_author import MirrorAuthor
from sdf_author import capsule, sphere, subtract

author = MirrorAuthor()
head = author.edit(sphere('head', (0, 0, 0), .2), 'Head', centerline=True)
tools = author.edit(capsule('groove', (.09, -.03, .18), (.1, .05, .18), .02),
                    'Carve both grooves')
root = subtract('carved-head', head, tools)
# Pass root and author.history to write_scene(...).
```

`--construction-seconds` scales the saved weights, including a short opening
hold, to the construction interval. The first 35% of a cut's interval previews
all cutters in the operation in red; commitment then publishes the actual
subtraction. Construction
holds the capture camera throughout. Presentation holds it during construction,
then orbits the finished model without changing the shading view. The build
preset uses 32 seconds of construction and ten seconds of orbit.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-build.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-build.json \
  --headless --time 4.73 --output /tmp/sdf-robot-cut-preview.png
build/dev/projects/sdf/sdf --config projects/sdf/presets/survey-robot-build.json \
  --headless --time 32 --validate-history --validate-field --require-validation \
  --output /tmp/sdf-robot-history.png
```

Previous edit, Next edit and the integer Edit slider seek to a committed state
and pause. Play/Space resumes from that state; Restart/R returns to empty.
Timeline covers the whole presentation. Camera input remains available while
paused. Cut preview toggles the color-only, depth-aware cutter visualization;
normal geometry and shadow queries use the published field. Export saves the
complete assembly and history with current material values.

Geometry rebuilds synchronously once per changed committed state. Camera,
preview opacity and unchanged paused states reuse the field. A failed history
build preserves the published state and pauses at the last successful edit.
`--validate-history` checks every prefix through native ray/material comparison,
then revisits midpoint, empty and final states and checks cache reuse.

Reproduce the authored native checks and presentation in fresh directories:

```sh
python3 projects/sdf/tools/qualify_authored.py --video --output outputs/sdf/robot-review-new
xvfb-run -a -s '-screen 0 1280x1100x24' env CUBEY_SDF_PRIVATE_XVFB=1 \
  python3 projects/sdf/tools/check_authored_window.py \
  --config projects/sdf/presets/survey-robot.json --height 1000 \
  --output outputs/sdf/robot-controls-new
```

The qualification pins native binary/shader hashes, runs eleven expression
fixtures and lifecycle/legacy checks, captures inspection views, replays the
explicit scene, measures matched GPU treatments and records a 16-second orbit.
It decodes all 480 video frames and screens the visible exhaustion color.
The private Xvfb check exercises resize/input and a material roughness edit.

Capture the build sequence, all 44 state images and its regression matrix:

```sh
python3 projects/sdf/tools/qualify_history.py --video --regressions \
  --output outputs/sdf/history-review-new
xvfb-run -a -s '-screen 0 1280x1100x24' env CUBEY_SDF_PRIVATE_XVFB=1 \
  python3 projects/sdf/tools/check_authored_window.py --history-controls \
  --config projects/sdf/presets/survey-robot-build.json --height 1000 \
  --output outputs/sdf/history-controls-new
python3 projects/sdf/tools/history_report.py --input outputs/sdf/history-review-new \
  --controls outputs/sdf/history-controls-new --output outputs/sdf/history-report-new
```

The native video is retained separately from its labeled copy. The report's
step viewer uses actual committed-state captures; labels are added to the video
after capture, with frame timing derived from the same saved history.

Reproduce the detail study, matched views and profiles:

```sh
python3 projects/sdf/tools/qualify_robot_detail.py --video \
  --output outputs/sdf/robot-detail-review-new
xvfb-run -a -s '-screen 0 1280x1100x24' env CUBEY_SDF_PRIVATE_XVFB=1 \
  python3 projects/sdf/tools/check_authored_window.py --history-controls \
  --config projects/sdf/presets/survey-robot-detail-build.json --height 1000 \
  --output outputs/sdf/robot-detail-controls-new
python3 projects/sdf/tools/robot_detail_report.py --input outputs/sdf/robot-detail-review-new \
  --controls outputs/sdf/robot-detail-controls-new --output outputs/sdf/robot-detail-report-new
```

`qualify_history.py --config PATH` can select another authored build preset.
Preview visibility stays a gate; `--hidden-preview-node NAME` explicitly declares
a cutter expected to be occluded from the fixed capture camera. Its on/off image
difference remains recorded. The detail study declares the rear vent cuts and
the potentially occluded far shoulder; only the three rear vents are hidden.

## Procedural recipes

Use the [stick-ball preset](presets/stick-ball.json) for the main visual scene:

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/stick-ball.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/stick-ball.json \
  --headless --motion presentation --capture video --fps 30 --output /tmp/sdf.mp4
build/dev/projects/sdf/sdf --headless --motion construction --time 4 \
  --count 257 --output /tmp/sdf-half.png
```

`--motion` selects `static`, `orbit`, `construction`, `presentation` or `carve`.
Carve requires one initial Fill followed only by removals, including when
loading an explicit recipe independently of the CLI scene name. The new preset
holds the solid for two seconds, shows four slow cuts through eight seconds,
accelerates until sixteen seconds and then orbits to twenty-four seconds.
`--no-cutter-cue` disables the introduction's depth-aware cutter-bounds outline.
Use `--motion static` with that preset for a completed-scene still or profile.
The default presentation is 24 logical seconds: eight for construction,
twelve for orbit, then traversal and brick diagnostics. `--duration`,
`--construction-seconds` and `--orbit-turns` adjust the timeline. `--time`
selects an exact headless still state. Motion videos default to duration times
FPS frames; an explicit `--frames` overrides that count. First/last capture
frames land at the timeline endpoints. Rendering and capture FPS do not decide
the edit prefix.

Reproduce the fixed native acceptance/capture matrix and assemble its review:

```sh
python3 projects/sdf/tools/qualify.py --output outputs/sdf/sky-qualification --video --windowed
python3 projects/sdf/tools/report.py --input outputs/sdf/sky-qualification \
  --output outputs/sdf/sky-review-export
```

The optional windowed check uses an isolated Xvfb server, libX11/libXtst and
FFmpeg. The headless matrix needs the app and CPU-test targets built above.
Receipts, applied configs, profiles, recipe and captures go under
the selected output directory; `report.py` prepares a separate static export.
Use a fresh `--output` directory when the binary or shaders change.

Negative-sticks captures use a separate bounded qualification path:

```sh
python3 projects/sdf/tools/negative_sticks.py \
  --output outputs/sdf/negative-capacity-new
python3 projects/sdf/tools/negative_sticks.py --atlas-axis 92 --details \
  --output outputs/sdf/negative-audition-new
python3 projects/sdf/tools/qualify_negative.py --video \
  --output outputs/sdf/negative-qualification-new
xvfb-run -a -s '-screen 0 1280x1024x24' env CUBEY_SDF_PRIVATE_XVFB=1 \
  python3 projects/sdf/tools/check_window.py \
  --config projects/sdf/presets/negative-sticks.json --height 850 --carving-controls \
  --output outputs/sdf/negative-qualification-new
python3 projects/sdf/tools/negative_report.py \
  --input outputs/sdf/negative-qualification-new --audition outputs/sdf/negative-audition-new \
  --capacity outputs/sdf/negative-capacity-new --output outputs/sdf/negative-review-new
```

`--validate-subtractions` checks intersecting cavities and a perforated-plate
fixture across promotion boundaries, backward/reset and 64/128-edit batching.
The negative-sticks guide/recipe does not change the underlying CSG or tracer.
The original hardware-filtered qualification measured 1.83 ms primary and 2.25 ms hard-shadow
median GPU time on the selected device. Geometry updates still synchronously
rebuild the field: roughly 48 ms median per build during logical carving.
See the scene evidence for p95 values, frame counts and image replay tolerance.

The fixed 1080p matrix uses 1× sampling and a closer default camera. The current
ground-disabled run measured 3.25 ms for primary rendering, 5.14 ms with hard
shadows and 11.39 ms with soft shadows. The presentation capture uses 2× sampling
at 720p. The evidence record retains the earlier ground-enabled measurements.
See the evidence record for frame counts, p95 values, capacity failures and the
sampling-error boundary.

The windowed panel supplies playback/scrubbing, scene regeneration, shading,
light and diagnostic controls. A full batch is retained as the reusable base;
the displayed partial tail is rebuilt when it changes. Backward scrubbing can
rebuild from the complete recipe. Updates wait for readers before replacing
either GPU field. Capacity failure stops publication, displays the completed
prefix and reports the limit; headless capture fails.

Atlas profiles are explicit: `--atlas-axis 64` (default, 262,144 bricks), `80`
(512,000) or `92` (778,688). All remain below a 2 GiB field resource budget.
The default 512-stick scene fits the first profile; the seed-42 2,048-stick
scene needs 92. The 6,400-stick stress scene exceeds even that profile and fails
without writing a successful capture. Profiles do not change sample spacing.

The field sample spacing is `1/(256*7)`. The hit tolerance is 0.2 sample and
normal differences are half a sample, clamped within the brick. An inside-solid
ray finds its exit. The CPU oracle bounds sampled visibility with the
1-Lipschitz trilinear error bound plus R16F allowance, and reports grazing
ambiguities separately. This does not establish analytic-surface pixel parity.
