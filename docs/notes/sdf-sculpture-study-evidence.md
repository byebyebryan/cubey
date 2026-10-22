# SDF sculpture study evidence

Date: 2026-10-22

Status: implemented, qualified and delivered for visual review under the
[four-subject plan](sdf-sculpture-study-plan.md). The user requested execution
with commits as work progresses. Native visual assessment below is the
assistant's; user acceptance remains separate.

[Open the native sculpture gallery, four construction films and all edit states](http://starship:8787/reports/b155234e48461677cf2e6d7bd69569f4f6fc3a79c87ac94e430ad4af1980ed08/).

## Clay audition

`tools/author_sculptures.py` writes two explicit blockouts per subject using
only existing primitives and operators. `tools/sculpture_audition.py` captures
hero, front, profile and the committed prefix before the first subtraction.
The 32 native Vulkan-validation captures passed. Receipts and images are in
`outputs/sdf/sculpture-audition/`; the manifest pins binary/shader hashes,
commands, effective source state and native geometry/build statistics. This
first audition does not run the complete ray/history qualification matrix.

| Subject | Leaves per variant | Selection | Visual finding and next revision |
| --- | ---: | --- | --- |
| Guardian | 20 | A, broad head and taller horns | Recognizable face with brow, nose, mouth and cavities. Improve horn taper, eye/mouth expression and the skull-to-neck transition; inspect the profile before ornament. |
| Vessel | 9 | A, fuller asymmetric belly | Mouth, interior, handle and three wall piercings read already. Refine opening rhythm and silhouette; confirm wall/base thickness and inspection from above. |
| Arch | 8 | A, lower crown | Front/rear cavities form a passage, but the smooth mass resembles a tooth. Add directed erosion that changes the outer silhouette and breaks its symmetry. |
| Driftwood | 12 | A, wider branch spread | Flow and fork are readable, but piecewise constant-radius capsules create swollen joins and blunt tips. Improve taper before adding grain-like grooves. |

The capsule stroke helper is deliberately retained for reproducing these
blockouts. A narrowly scoped rounded tapered segment is the shared capability
to evaluate next: horns and branches expose its need directly in native views.
No evidence here establishes a completed sculpture or a successful material
treatment.

```sh
python3 projects/sdf/tools/author_sculptures.py --stage blockout
python3 projects/sdf/tools/sculpture_audition.py \
  --output outputs/sdf/sculpture-audition-new
```

## Progress

- [x] Two clay variants and native shaping checkpoints for all four subjects.
- [x] Shared tapered-stroke decision and generic qualification/presentation.
- [x] Guardian modeling, construction presentation and native qualification.
- [x] Vessel modeling, construction presentation and native qualification.
- [x] Arch modeling, construction presentation and native qualification.
- [x] Driftwood modeling, construction presentation and native qualification.
- [x] Published comparison gallery, delivery review and study verdicts.

## Shared foundation

The [rounded tapered segment](sdf-rounded-stroke-contract.md) is implemented in
the CPU evaluator, GPU evaluator and cutter preview. CPU numerical-reference,
capsule-equivalence, containment and gradient checks pass. The six selected
repository checks pass, including authored/recipe tests, shader dependencies,
rendering and layer policy.

`outputs/sdf/sculpture-foundation/` records the native authored regression
matrix, including four new rounded-taper fixtures, existing expressions,
lifecycle, legacy scenes, replay and material/cache checks. Every run passes.
The receipt records binary/shader hashes and the source state used to build it;
later content qualification will pin the committed source separately.

`qualify_history.py` now accepts an export basename and uses the scene's title
in video captions. The default export remains `robot.json` for compatibility.
`qualify_sculpture.py` accepts per-subject view/build metadata and explicit
occluded-cutter reasons, then reuses native history qualification. A first run
on the nine-leaf vessel blockout passes all prefixes and preview visibility,
static/replay and export/reload identity, an axis-80 atlas relocation and
subject/robot performance captures. It is retained in
`outputs/sdf/sculpture-foundation/generic-vessel/`. No robot-specific camera or
hidden-preview exception is inherited by this workflow.

The accepted 82-shape field-kit robot was also captured with the new binary;
its 960×960 RGB pixels match the preceding joint-review hero exactly (maximum
channel difference zero). The comparison capture is
`outputs/sdf/sculpture-foundation/robot-fieldkit.png`.

## Modeling revisions

Four native refinement passes are retained under
`outputs/sdf/sculpture-refinement-{1,2,3,4}/`. The first pass exposed disconnected
arch geometry and short punctures where long grooves were intended. The arch
cut was reoriented. `sdf_probe.py` now places authoring cutters along the actual
front surface of the saved expression. It is a build-time double-precision
interpreter, not a replacement for the native CPU/GPU rendering oracle.

The retained capsule blockouts reproduce unchanged. Finished curved strokes
sample a Catmull-Rom authoring path into explicit rounded tapered segments.
Hard union joins those segments; smooth union remains at major branch/root
connections. Polynomial smoothing between every small segment inflated each
join and made corrugations more apparent. A sampled curved ceramic handle and
teardrop piercings were also less clean than the simpler analytic handle and
round through-holes, so the vessel keeps the latter. This study does not add
an exact spline sweep or claim these sampled curves have no approximation.

| Candidate | Leaves | Instructions | Modeling changes |
| --- | ---: | ---: | --- |
| Guardian | 76 | 151 | Tapered swept horns, small ears, shoulder/neck mass and surface-following mouth/cheek/brow cuts. |
| Vessel | 10 | 19 | Rounded hollow belly, open lip, fused analytic handle and three confirmed wall-to-interior piercings. |
| Arch | 51 | 101 | A filled core, joined front/rear cavities, a lower undercut, exposed rock planes and directed surface grooves, with both supports retained. |
| Driftwood | 94 | 187 | Curved tapered trunk/branches, a rear branch, hollow broken ends, a knot recess and tapered grooves following the form. |

`sculptures.json` holds subject-specific inspection cameras and design-intent
probes. `check_sculptures.py` evaluates separately chosen world-space solid
paths and empty passages against the saved scene. All current probes pass.
Reintroducing the earlier arch cut's position/extent/orientation makes the
right-support check fail; that negative control is recorded at
`outputs/sdf/sculpture-arch-bad-support-audit.json`. These sampled intent checks
complement native rays and visual inspections; they are not a complete topology
proof or an artistic acceptance result. Full final qualification follows.

## Construction review corrections

The first complete guardian run passes all 77 distinct states, all 31 cutter
visibility pairs, 1,500 video frames, export/reload and atlas relocation. Its
final replay is pixel identical to the static scene. The full receipts are at
`outputs/sdf/sculpture-guardian-qualified/`. Driftwood also completes its native
matrix at `outputs/sdf/sculpture-driftwood-qualified/`.

The vessel's initial run stops at the visibility gate: `belly-interior` is
fully hidden behind the front wall. It expands the chamber after the visible
main hollowing operation. This single named cutter now has a documented
exception in subject metadata; the overhead interior view remains required.
The main hollowing and all three wall piercings still need visible previews.
The stopped run is retained at `outputs/sdf/sculpture-vessel-preview-attempt/`.
The film's chapters choose the visible hollowing, handle and piercing steps.

A more substantial issue was found in the arch's construction story. Its
initial mass already had an opening between the supports: the cuts only
enlarged that gap. Native blockout images and saved-tree probes establish the
mistake. The finished author now fills a central core before carving. New
prefix-intent checks require a solid center before the first cutter, a front
void with a retained back wall before the rear cutter, and a through-passage
afterward. The previous construction fails those new checks; its otherwise
passing native renderer matrix is retained separately at
`outputs/sdf/sculpture-arch-open-blockout-qualified/`.

The new saved-tree probes measure distance -0.170 at the initial center,
+0.12445 in the first front cavity, -0.00987 in its retained back wall, and
+0.12152 at the opened rear sample in the final scene. Those sign changes test
the intended operation, beyond checking the final object's appearance. The
new arch and the vessel's explicit preview policy are requalified separately.

The filled core also closes the base, initially producing a rock window. One
subsequent capsule undercut opens the lower passage and separates the feet,
restoring the intended arch silhouette. Its added empty-path check passes
while both retained support paths remain solid. The native hero was inspected
before the final capture. The intermediate closed-window qualification is
retained at `outputs/sdf/sculpture-arch-window-qualified/`.

## Final native qualification

Each selected scene has a deterministic generator, explicit scene, static/build
presets, six material inspection views, hero clay/blockout comparisons, every
committed state and a 50-second film (40 seconds construction, ten orbit).
Outputs are `outputs/sdf/sculpture-{subject}-qualified/`. Native matrix starts
are clean at the source revisions recorded below; later shared-author changes
only add the arch's construction corrections. Per-subject snapshots retain the
exact generators, scenes, configuration, native binary and thirteen shader
hashes used by that capture.

| Subject | Capture source | Distinct states | Native history comparisons, including revisits | Rays | Direct CPU/GPU points |
| --- | --- | ---: | ---: | ---: | ---: |
| Guardian | `086d90cc` | 77 | 80 | 33,514 | 82,122 |
| Vessel | `23b8423a` | 11 | 14 | 4,408 | 12,294 |
| Arch | `7e55b2b1` | 52 | 55 | 20,437 | 52,081 |
| Driftwood | `db065c0b` | 95 | 98 | 44,476 | 106,458 |

These final history runs total 235 distinct states, 247 comparisons, 102,835
rays and 252,955 direct points. There are zero reported mismatches, exhausted
rays, material/part disagreements or finite-grid sampling ambiguities across
this matrix. Existing tolerances are unchanged; future scenes may still report
sampling ambiguities under that established contract. Material-only cache
checks pass. Every final static/replay image is identical, and export/reload
preserves both scene bytes and the hero image. Axis-80 atlas relocation gives
identical images under the same native oracle.

Regenerating both blockout and final stages reproduces every scene/preset byte
and all four qualified source-scene hashes. The separate reproduction receipt
is `outputs/sdf/sculpture-reproduction.json`.

All 117 cutter on/off pairs preserve geometry/appearance identity. The 116
front-visible previews meet the existing image gate; only the documented
`belly-interior` preview is hidden. All 6,000 native video frames decode with
zero visible magenta exhaustion pixels. Each film visits every edit and builds
exactly its edit count plus one fields, including the empty workspace.

The same binary/shaders identify the 41-run foundation matrix, the four final
content matrices and private Xvfb controls. Each subject passes resize,
orbit/zoom, next/previous, empty/final/midpoint scrubbing, stable paused cache,
restart, playback and Escape. The control check now waits for real committed
progress: a ten-edit history scales its empty opening hold longer than the
robot's old fixed wait. Receipts are under
`outputs/sdf/sculpture-controls/{subject}/windowed-interaction.json`.

## Costs and visual verdicts

RTX 5070 Ti / 610.57.04, 1920×1080 at 1×, hard shadows, 180 frames with 30
warmup and 148 completed GPU samples per profile. Steady frame time sums
tracing/shading and shared post. Build wall time is separate and synchronous;
readback/encoding are excluded from frame costs.

| Subject | Cold geometry build | GPU median / p95 | Final active bricks | Peak committed-state bricks |
| --- | ---: | ---: | ---: | ---: |
| Guardian | 69.154 ms | 2.231 / 2.326 ms | 107,422 | 107,422 |
| Vessel | 13.090 ms | 2.002 / 2.106 ms | 140,780 | 141,470 |
| Arch | 61.690 ms | 2.296 / 2.357 ms | 143,060 | 158,176 |
| Driftwood | 56.658 ms | 1.981 / 2.050 ms | 70,531 | 70,531 |

Each uses the same 912,882,864-byte axis-64 field allocation and 262,144-brick
capacity. No geometry/program or field-allocation ceiling was raised. Different
shapes cover different pixels and occupy different bricks; these are bounded
content measurements, not a claim of a general renderer speedup. Matched robot
profiles are retained in each receipt.

The guardian and vessel are the stronger visual candidates in the assistant's
assessment. The guardian has a recognizable expression and fused silhouette;
rear lighting still reveals some transitions between horn segments. The vessel
is the cleanest hollowing example, and simpler analytic geometry won its handle
comparison. The arch's corrected breakthrough is a clear SDF operation, while
its final form remains a stylized carving rather than convincing wind erosion.
Driftwood establishes curved stroke placement, joins and hollows, but its grain
and branching remain simplified and some tangent changes are visible.

The study supports scripted authoring as a useful content route. It also shows
why more primitives alone do not solve composition, why final-object checks
cannot prove a construction story, and why a true curve sweep would be a more
focused next geometry experiment than a broad catalog of new shapes. These are
four reviewable experimental results; technical passage does not imply user
acceptance or four equally successful artworks.

### User review and selected follow-up

After delivery, the user ranked the guardian strongest and driftwood weakest,
and requested planning a further detail pass on those two. The
[refinement plan](sdf-guardian-driftwood-refinement-plan.md) develops the
guardian's existing character and addresses driftwood's forms and wood identity
before fine grain. This records the user's relative ranking, not acceptance of
every modeling detail or an implemented second pass. Vessel and arch remain
unchanged comparison subjects.

## Gallery delivery

The gallery packages native material/clay/blockout views, all 235 state images,
four films, seventeen video chapters, per-subject evidence and downloadable
source. Browser checks visit every state image, all six inspection views per
subject, all material modes, previous/next boundaries and keyboard scrubbing.
All seventeen chapters seek to the expected caption and all four films play.
Selected-edit seeking also works for each subject. The final reset guard hides
unloaded video controls and prevents a stale caption after switching subjects.
The 390px layout has no document overflow and preserves the 4:3 film ratio.
No page errors or warnings are reported. The published URL's breakthrough
chapter was checked again after delivery.

The full export initially exceeded the publisher's 100 MiB bundle limit.
`sculpture_report.py` now packs all auxiliary cutter on/off PNGs and desktop
screenshots in `auxiliary-captures.tar.xz`, retaining their original paths and
byte hashes. Every archived file is restored into memory and hash checked
before removing only its duplicate in the export. Original qualified outputs
are untouched. Every film, state and inspection image remains directly
accessible. The published bundle is 103,308,013 bytes across 1,483 files.

HTTP delivery verifies the entrypoint bytes. Separate checks reproduce the
served HTML, lossless archive and arch presentation film exactly. Shared report
hosting remains running. Receipts and screenshot:

- `outputs/sdf/sculpture-browser-review.json`
- `outputs/sdf/sculpture-report-delivery.json`
- `outputs/sdf/sculpture-gallery.png`
- `outputs/sdf/sculpture-study-summary.json`
- `outputs/sdf/sculpture-reproduction.json`

The published snapshot retains its pre-publication browser receipt; the local
receipt additionally records the published-URL check. Git retains source,
presets and these evidence links; generated media lives in outputs and the
immutable report snapshot.

```sh
python3 projects/sdf/tools/author_sculptures.py --stage final
build/dev/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-build.json
python3 projects/sdf/tools/qualify_sculpture.py --subject guardian --video \
  --output outputs/sdf/sculpture-guardian-new
```

Select `vessel`, `arch` or `driftwood` in the corresponding generator,
qualification command and preset names. Static presets omit `-build`.
The retained blockout stage remains independently reproducible. Reports are
assembled with `sculpture_report.py --controls-root ... --output ...` after all
four native matrices and desktop receipts are available; run browser checks
before publishing a fresh export.
