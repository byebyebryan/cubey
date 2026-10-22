# SDF sculpture study evidence

Date: 2026-10-22

Status: in progress under the [four-subject plan](sdf-sculpture-study-plan.md).
The user requested execution with commits as work progresses. Native visual
assessment below is the assistant's; user acceptance remains separate.

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
- [ ] Guardian modeling, construction presentation and native qualification.
- [ ] Vessel modeling, construction presentation and native qualification.
- [ ] Arch modeling, construction presentation and native qualification.
- [ ] Driftwood modeling, construction presentation and native qualification.
- [ ] Published comparison gallery, delivery review and study verdicts.

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
