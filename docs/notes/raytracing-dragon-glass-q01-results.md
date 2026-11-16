# Dragon glass Q0.1: thin-fold exits restored

2026-11-09. Implements the [numerical replay plan](raytracing-dragon-glass-q01-plan.md).

**The original medium failures are spawn offsets crossing opposing faces in
very thin folds.** The correction uses a fixed, model-anchored tracing frame
and removes the crease helper's extra factor of two. The donor error-bound
calculation, certified geometry, physical scene/camera settings, materials,
shared atmosphere/sun and production budgets remain unchanged.

All five seed-7 controls and all six independent full-HD 4096-SPP
canonical/elevated/grazing seed-19/43 references pass with zero forbidden
transport status. The ordinary build matches the frozen candidate with
diagnostics disabled. Q1 smoothing, Q2 filtering and Q3 reconstruction remain
separate work. This is a finite qualification on the recorded GPU/driver, not a
universal guarantee for arbitrary sub-resolution geometry or unbounded paths.

## Diagnosis and replay fidelity

The frozen baseline reproduces the full-HD seed-7 failure at pixel `(908,344)`,
zero-based sample `1335`. Passive journal instrumentation reproduces its status
map, with a measured RGB rounding effect: full-image HDR RMSE `1.38212e-5`,
maximum component difference `.00223881`. Its isolated replay retains the full
logical extent, camera, seed and sample; all **86 raw uint32 packets match**
the full-prefix journal. It is one diagnostic sample, not a 1336-SPP image.

The first failure is an inside miss on reflected child branch 3 at depth 5.
The previous entry is primitive `220337`, hardware front-facing, with incoming
geometric dot `−.575308` and transmitted dot `−.838247`. This is not an observed
grazing-facing disagreement or queued-reflection side error. Exact rational
predicates on unchanged binary32 vertices/transforms show that the displaced
origin crosses opposing primitive `220311`. The triangle interior tests are
valid; no arbitrary shared-edge tie is selected.

| Recorded baseline case | Original seed 7 | Independent seed 19 |
| --- | ---: | ---: |
| Pixel / logical sample | `(908,344)` / `1335` | `(897,320)` / `1986` |
| Branch / failed depth | 3 / 5 | 4 / 3 |
| Entry / opposing primitive | 220337 / 220311 | 199473 / 199468 |
| Exit distance from reconstructed point along outgoing ray | .329894 µm | .164599 µm |
| Scalar spawn offset | .350869 µm | .192889 µm |
| Actual rounded displacement length | .362025 µm | .174573 µm |
| Exit parameter after displacement | −.077838 µm | −.071955 µm |
| Crease amplification selected | yes | no |

The first lost branch carries RGB weight `(.0162551,.0192465,.0176293)`. A causal
private variant changes only that spawn: the first 75 packets remain identical,
the GPU finds the expected exit, and the branch completes without discarding its
weight. A crease-only candidate passes seed 7 but fails independent seed 19.
That second failure is bracketed by ordinary 1986/1987-sample prefixes; its
121-record first-failure search and isolated replay match exactly.

## The correction

The optical dragon is translated by `(0,.353489995,0)` in the physical scene.
Although this is a small scene, translation terms dominate the donor bounds
at its submicrometre folds. The body now defines a fixed tracing origin.
Shading and TLAS transforms and tracing camera uniforms share that frame.
Original vertex data is retained; vertices are not baked into rounded world
coordinates. Admission checks, physical camera controls, saved views and shared
atmosphere/background preparation continue in physical world coordinates.

The unchanged donor calculation predicts base offsets of roughly **31/98 nm**
at the two recorded points, compared with **157/193 nm** in the original frame.
Rebasing also changes which neighboring planes need crease treatment. The final
displacement need not always be shorter: direction and plane clearance matter.
The crease helper projects the existing scalar bound onto participating face
normals without the previous extra ×2 amplification. No primitive is ignored,
culling removed, direction flipped, medium toggled or positive branch dropped
to obtain acceptance. Raw throughput cutoff remains zero.

The numerical rationale follows the original
[NVIDIA offset analysis](https://developer.nvidia.com/blog/?p=68307), which
separates reconstruction and transform error and discusses translating the
rendering frame. [PBRT's rendering spaces](https://www.pbr-book.org/4ed/Cameras_and_Film/Camera_Interface)
also separate physical scene coordinates from a translated rendering frame.
The model-anchored choice keeps the frame fixed during orbit/dolly and preserves
acceleration-structure reuse.

A two-boundary GPU fixture reuses the production reconstruction/spawn expressions
with the original source barycentrics and outgoing directions. Both baseline
spawns miss. The corrected spawns find **220311 and 199468**, with positive query
distances about **.310242 and .321138 µm**. This tests the original interfaces
directly rather than relying only on changed camera trajectories.

## The original event-limit sample

The canonical seed-19 baseline also has an event-limit pixel `(671,388)`.
A CPU-command-buffer probe copies existing query records between dispatches
without changing any shader binary. Its full 4096-SPP HDR is byte-identical to
the frozen baseline and identifies zero-based sample **3288**.

A final-sample-only optics readback accumulates the real prefix using the
unchanged compact HDR route, then changes only the output mode on its last
dispatch. That mode is consumed after tracing. Its target RGB matches the
baseline prefix exactly. At the production budget it reaches depth 128 with
discarded RGB weight `(1.67158e-6,.000404581,2.33581e-5)` and 73 TIR events.
At the deeper budget it completes at depth 129 with no discarded weight.
This is a real bounded-transport limit in that baseline path, not a zero-weight
termination. The final candidate passes the full cumulative seed-19 reference
with the original 128-event budget. No universal maximum-path-length claim follows.

A rebase-only prototype passes five of six references and fails elevated seed
19 at `(1007,752)`, logical sample 808. The combined correction passes that view.
An additional minimal probe retains its exact target RGB and identifies a
shadow inside miss. Exact predicates show the recorded spawn segment crosses
exit primitive `805124` at parameter `.80009969`, before reaching its endpoint.
It is another offset/clearance case, covered by a separate retained fixture.
Failed prototypes, nonreproducing journal searches and the stopped all-sample
full-HD optics probe remain separate from accepted measurements.

## Qualification and preservation

- Production: 128 events / 1024 radiance / 4096 total queries, cutoff zero.
  Seed-7 full-HD prefixes 1335, 1336 and 2048, plus doubled-budget and shadows-off
  2048-SPP controls, have no forbidden transport status.
- Full HD: canonical, elevated and grazing views, seeds 19/43, 4096 SPP each.
  All six full-image cumulative status maps are clear of medium, event, query,
  shadow and stack failures; finite/nonnegative HDR and Vulkan validation pass.
  These maps cover the named failing samples and neighboring pixels/samples.
- Matched canonical seed-19, 4096-SPP raw RGB changes have whole-image RMSE
  `.00403723`, maximum component difference `5.91844`, and mean luminance
  `.50129467` before / `.50129023` after. The changed arithmetic/spawns alter
  sampled paths. The corrected seed-19/43 difference is `.142943` RMSE, supplied
  as variance context rather than a confidence interval or a quality claim.
- The normal executable with diagnostics disabled produces a byte-identical
  2048-SPP PNG to the frozen candidate. Nine participating SPIR-V files and all
  36 runtime source files match that candidate. No journal buffer/binding exists
  on this path; query storage is 64 bytes.
- Twenty RT CTests pass, including asset-independent exact geometry and trace
  frame invariants. Four documentation checks, eight acceptance-policy tests and
  seven Q0 analysis tests are recorded separately. Fourteen non-optical smoke
  PNGs are byte-identical before/after.
- The optical qualifier passes IOR-1, neutral/tinted, default/deep, base
  isolation, effective-view roundtrip and admission controls. The original
  opaque dragon qualifier passes. Native owned-Xvfb edit/reload/orbit/dolly/resize
  passes with three AS builds. These do not claim physical desktop latency.
- GPU active tracing is reported separately from setup and native presentation.
  Memory accounting is unchanged on the ordinary route; private journal storage
  has its own checked component.
- Both GLB hashes, material recipes, certificates and existing presets are
  preserved. The separate dirty SDF worktree is untouched. No commit, push,
  public-site update or Q1–Q3 work occurred.

## Diagnostic coordinates and next boundary

Optical position records and journals use tracing coordinates. Metrics and new
journals expose `trace_world_origin`; add it to recover physical world points.
Other scenes retain a zero origin. CPU scene exports remain in their physical
scene convention. Raw record sizes and non-optical behavior are unchanged.

The Q0.1 correctness hold is cleared for this qualification matrix. Faceted
interfaces, solar variance and one-sample motion are the separate Q1–Q3 subjects.
Resume the guarded optical-normal/footprint study next; this pass adds no
smoothing, denoising, rough glass, GI, caustics or clouds.

[Retained replay and qualification evidence](../evidence/raytracing-dragon-glass-q01-2026-11/README.md)
contains the exact failing baseline, rejected prototypes and accepted candidate.
