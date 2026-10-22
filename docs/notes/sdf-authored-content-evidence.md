# SDF authored content evidence

Date: 2026-10-21

Implementation follows the [authored content plan](sdf-authored-content-plan.md).
This record distinguishes native checks and assistant visual selection from
the user's assessment of the resulting content.

[Open the native robot review](http://starship:8787/reports/eafb4462b9072db9f445b21e5d9402466d78e11fd70e23fdcba7d2bf263c8540/)
for the turntable, ten inspection views, blockout alternatives and receipts.

## Robot blockout

`projects/sdf/tools/robot_blockout.py` authors three original silhouettes using
the existing rounded-box/sphere recipe format. Twelve native 960x960 views
cover front, side, rear and three-quarter compositions. Each requires Vulkan
validation; the three hero views also pass the CPU/GPU ray oracle. Receipts
and explicit recipes are under `outputs/sdf/robot-blockout/`.

The assistant selected the medium survey proportions for further work. The
tall version has a less compact body/head relationship; the broad version has
separated leg attachments that would need adjustment. The selected blockout
is recognizable as a toy robot but still needs material separation, an authored
pose and more deliberate detail. This selection is not user visual acceptance.

The new `--clay` option supplies a neutral base color for shape inspection.
Existing scene defaults and recipe geometry remain unchanged. The blockout
renders use hard self-shadows, the shared atmosphere and no ground.

## Grouped geometry and material foundation

The version-2 `cubey.sdf.scene` format retains named CSG structure, transforms
and a material table. Its bounded postfix program supports boxes, spheres,
capsules, capped cylinders, union, subtraction and polynomial smooth union.
Rigid transforms and positive uniform scale preserve the selected distance
bound. The elementary primitive formulas were implemented directly from their
Euclidean definitions; no external primitive/operator source was copied.

The authored builder classifies complete composite expressions and samples the
same R16F brick representation. The existing batch builder remains available
for legacy recipes. Both use the shared tracer and HDR/atmosphere presentation.
Materials are resolved at surface hits without additional appearance atlases.
Opaque base color, roughness and emission are supported. Native part/base-color/
roughness diagnostics and desktop material editing expose that result.

Fixtures in `outputs/sdf/authored-fixtures/` pass required Vulkan validation,
the sampled-field CPU ray oracle and direct CPU/GPU expression/material checks.
They cover sphere, capsule, cylinder, isolated group cuts and smooth blending.
The material-only cache check retains one field build. The legacy cut fixture
still passes 263 rays without mismatch, exhaustion or sampling ambiguity.
The recipe/timeline and authored scene CPU test targets pass.

Coincident material selection keeps the earlier branch within a 1e-6 distance
tie band; geometry still uses the actual min/max. This stabilizes material
ownership without altering the distance bound. Nested smooth material blends
evaluate parameters through the full expression instead of retaining only
two material IDs. No image textures or UV coordinates were introduced.

## First colored survey robot

`tools/author_robot.py` uses the external `sdf_author.py` helper to emit the
complete named scene at `projects/sdf/scenes/survey-robot.json`. The new
`presets/survey-robot.json` selects it directly; no Python runtime is needed to
load or render the saved scene. Regenerating the file is a build-time content
operation.

The first colored version has 43 primitive leaves and 85 instructions. It
retains the survey blockout proportions, tilts the head, raises one arm and
adds local eye/vent cuts, a backpack, joint separation, dark soles and four
opaque materials. It uses capped cylinders for the optic and shins, capsules
for limbs and the antenna, and a smooth union for the torso/hip transition.
The material boundaries remain sharp on the robot.

Its first native 960x960 hero passes 425 rays with zero mismatches or exhausted
rays and four sampling ambiguities under the existing finite-grid oracle.
Maximum surface residual is 0.000117736; the largest nominal first-hit
difference is 0.0150168. Direct CPU/GPU evaluation passes 1,030 points with
maximum distance error 5.96046e-8, zero material error and no part mismatch.
The material-only cache check retains one field build. The field uses 124,114
of 262,144 bricks in the axis-64 profile, allocating 912,874,672 bytes.

These numbers identify the initial capture before the final multi-view,
interaction and performance qualification. The image is
`outputs/sdf/robot-first-colored.png`; its explicit exported scene is beside it.

## Final native qualification

The final runtime source is `7c1a37eee963ffcb6d961aed810e6afe9c297420`,
with a clean worktree at capture start. `outputs/sdf/robot-qualified/manifest.json`
records the binary and 13 compiled shader hashes, applied settings, commands,
device/driver, captures and timing samples. Final geometry fingerprint is
`583e07fe12717184`; appearance fingerprint is `4a91b557ad4f421d`. The last
content refinement exposes the previously buried ivory backpack spine.

The qualification passes all eleven expression fixtures: sphere, capsule,
cylinder, isolated local cut, inherited cut material, smooth union, nested
material blending, transformed group, thin gap, workspace clipping and the
64-leaf boundary. Each requires Vulkan validation, the sampled-field ray oracle
and direct CPU/GPU distance/material/part agreement. The robot passes 425 rays
with 242 hits, zero mismatches, zero exhaustion and four finite-grid sampling
ambiguities. Its maximum nominal first-hit difference is 0.0150168; maximum
surface residual is 0.000117736. Direct comparison passes 1,030 points with
maximum distance difference 5.96046e-8, zero material error and no part mismatch.
Those tolerances were not relaxed for the new content.

Native lifecycle checks pass failed-build rollback, material-only cache reuse,
legacy/authored transitions, sequential batch/reset boundaries and subtraction
fixtures. The intentionally over-capacity authored build preserves the previous
published nodes, statistics and build count. The explicit final scene exports,
reloads and exports byte for byte, with both fingerprints retained. This is a
scene-persistence result, not a claim of bit-identical replay images. The legacy
cut and 2,048-stick scenes pass their ray checks; authored carve playback is
explicitly rejected before capture.

Final captures cover hero, front, side, rear, detail, clay, part identity,
base color, roughness and normals. The 16-second 1280x960 turntable has 480
frames at 30 FPS with 2x internal sampling. All encoded frames were decoded and
screened for the visible magenta traversal-exhaustion color; none met the
recorded RGB threshold. This screening supplements the native ray fixtures.
Assistant inspection found no obvious assembly gaps in the fixed views or
sampled turntable frames. The rear stays darker under the fixed sun. These
observations are separate from the user's judgment of the design.

`outputs/sdf/robot-controls/windowed-interaction.json` records an isolated Xvfb
run of the final desktop application. Resize, orbit, zoom, pause/reset and
Escape pass with Vulkan validation. A real ImGui roughness edit changes ivory
from 0.52 to 0.922, changes the appearance fingerprint to `5157ef13bccbb94f`
and retains geometry `583e07fe12717184` with one field build
(`field_rebuilt=0`). The native screenshot records the edited control. The
authored panel disables the legacy cutter cue and separates clay roughness
from the material table.

The CPU recipe/authored targets and four renderer/build policy checks pass
(six selected CTests). Documentation navigation passes. All ten selected native
SDF documentation recipes parse successfully with unavailable GPU/display
environment variables, using the existing template early-exit check. This is
parser evidence; native execution is covered above. The repository-wide CLI
documentation matrix was not qualified because unrelated apps are unbuilt.

## Costs and retained limits

The selected device is NVIDIA GeForce RTX 5070 Ti, driver 610.57.04. Matched
1920x1080, 1x, static-robot profiles submit 180 frames, discard 30 warmup frames
and retain 148 completed GPU timestamps per treatment. Summed tracing and shared
post times exclude field construction, readback and video encoding.

| Treatment | Median GPU frame | p95 | Completed samples |
| --- | ---: | ---: | ---: |
| Authored materials, shadows off | 2.194 ms | 2.322 ms | 148 |
| Authored materials, hard shadows | 2.505 ms | 2.602 ms | 148 |
| Clay override, hard shadows | 2.341 ms | 2.492 ms | 148 |

The clay comparison changes appearance on the same geometry/lighting; its
roughly 0.16 ms difference is not an isolated material-stage timer. Legacy
rendering has a separate compiled fragment variant so it does not carry the
authored expression/material stack. Common surface code remains shared.

The final field uses 125,200 of 262,144 bricks, allocating 912,874,672 bytes
(870.6 MiB) for field resources. This excludes frame targets, material/render
buffers and driver allocations. The new fixed field program buffer accounts
for 8,208 bytes; no color/material atlas was added. The hero full build takes
43.7644 ms wall time, with 0.671648 ms filtering and 42.0749 ms brick sampling
GPU time. Geometry changes still wait for the GPU and rebuild synchronously.
Material edits preserve the distance cache but also wait before uploading their
small replacement data. The offline turntable does not qualify smooth geometry
editing or interactive sculpting.

The scene supports 64 primitive leaves, 128 instructions, 16 materials,
rigid/uniform transforms, hard union/subtraction and one smooth-union operator.
The robot uses four opaque dielectric materials with base color, roughness and
small sensor emission. No procedural noise, UV/image textures, metallic or
glass rendering, environment specular, GI, terrain, ground or part animation
was added. Specialized generation/import and a general editor remain later
experiments. The first authored object is ready for user visual review.

## Delivered review

`projects/sdf/tools/authored_report.py` assembles the dedicated static export at
`outputs/sdf/robot-final-review/` from the qualified native outputs, blockout
audition and windowed receipts. The report links the preceding procedural
prototype and Cubey catalog. Publication verified the served HTML bytes; an
isolated Chrome session also passes five decoded chapter seeks, playback, all
ten view controls/image links, image decoding, local artifact links, a 390px
layout and absence of page JavaScript errors. The static publisher's lack of
Range support is handled by downloading the MP4 into a seekable browser Blob.
Local and published-URL browser receipts are
`outputs/sdf/robot-final-browser-review.json` and
`outputs/sdf/robot-final-delivery.json`.

The native runtime capture remains pinned to `7c1a37ee`. Subsequent changes
only document/present the result and make the already-completed full-frame
video screening reproducible in the qualification tool without NumPy. That
standard-library screening reproduces the original screening receipt exactly.
No native source, shader or scene changed after the recorded qualification.
