# Saved-landscape wide-view rendering study V1

Verdict: retain one explicit lighting-only experiment,
`--terrain-readability filtered-study`. It reduces the distant terrain peppering
in these frozen views without changing geometry, captured fields or water
coverage. `refined` remains the rendering default; no-argument Terrain startup
remains `legacy`. Native GUI and joint visual/default review are deferred.

Browser comparison:
<http://starship:8787/reports/55f06e5da5dce2a094a9b5004154524074f6915b96ac0b34929cc317e77c9b85/>.
The opening comparison changes only ground lighting. View/treatment selectors,
the filter toggle, full-resolution PNG links and boundary view expose the
diagnostic controls. This is a published static report, not a native Cubey UI.

## Evidence and interpretation

The baseline is
`outputs/terrain/wide-view-study-20261013-wSNAsL`; the trial is
`outputs/terrain/wide-view-trial-20261013-RCCBO6`.
All 51 installed input identities are retained. No generator, solver, new water
bake or rainfall tuning ran. Each state is held after twelve simulated hours at
512 mm/h, intentionally exaggerated visual-demo rain rather than normal weather
or a permanent-water inventory.

Twenty-nine baseline ablations cover local/wide mountain views, an independent
wide valley state and a deliberately wet finite north edge. Camera, state,
presentation clock, mesh-detail focus and shadow fit stay locked within each
comparison. The 32m and 64m states are independent simulations, not water LOD.

- Distant terrain grain survives hidden water, clean clay, zero sun-shadow
  strength and denser uniform geometry. The normal diagnostic also retains the
  fine slope structure. The filtering trial supports a lighting-normal
  undersampling diagnosis; these controls do not prove that every far artifact
  has that cause.
- Native median depths are 3.94cm (mountain/32m), 13.32cm (mountain/64m) and
  8.79cm (valley/64m). Their 90th percentiles are 1.97, 5.02 and 7.30m. Broad
  shallow flooding is present in the saved model; shading is not inventing all
  of the blue sheets. These are native-cell statistics, not projected wet area.
- North-edge maxima are 2.90, 7.00 and 27.86m. Water nevertheless ends at the
  capture rectangle. That cutoff is missing coverage, not a naturally dry shore.
  Neither normals nor fog are used to disguise it.
- Zero shadow strength does not fix this grain and still renders the shadow
  pass. The far shadow receivers remain outside the qualified core-focused
  shadow footprint; this study does not provide cascades or wide shadow coverage.

## The one retained trial

The shared paired-ground fragment body uses a four-point central difference of
bilinearly reconstructed immutable bed height (four bed reads at each point).
The radius follows pixel footprint, is bounded to 1–8 native cells, and blends
in between footprints of 1–3 cells. It fades out before the capture/context seam.
Pixels below the footprint threshold retain their original normals; grazing
cliffs can activate filtering even in a close camera view.

This changes direct/ambient lighting normals only, after material classification.
It adds no resources, passes, elevation edits or hydraulic work. Vertex positions,
paired terrain/water triangulation, source/climate masks, wetness reconstruction,
water coverage and water shader are unchanged. Exterior context has no trial
filter. The raw-normal ambient-visibility heuristic is also unchanged. The option
is wired through shared saved-water runtime preparation and the existing capture
appearance combo; it is not exposed/promoted in live Fluid or the radial backdrop.

This is not a true prefiltered height mip hierarchy, an exact pixel-footprint
integral, or a universal anti-aliasing solution. Large-scale ridges remain readable
in the studied views; temporal stability and a broader camera/seam envelope still
need visual review before any promotion.

The established direction is described in
[GPU Gems 2, GPU-based geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry):
prefiltered elevation levels, level-matched/blended normals, and a four-filtered-
height-sample pixel-normal alternative. Cubey's bounded stencil is an inexpensive
approximation motivated by that direction, not a port of its full clipmap system
or evidence-equivalent implementation.

## Qualification

- All 42 selected regression groups pass without skips, including paired-gap,
  context-edge, saved-water lifecycle and shared Fluid Scenic/whitewater GPU
  checks. Six portable helper tests cover camera locking, explicit trial
  selection, frozen-identity rejection and incomplete/failed report evidence.
- All 29 baseline, 14 trial and two additional close frames audit exact GPU bed,
  depth and decoded velocity. The opening baseline is pixel-exact to the
  previous first-use frame; seven default re-renders remain pixel-exact after
  adding the optional filter.
- The 6.5km close pair preserves the foreground presentation. The central near
  crop [320,540–960,720] differs in one color channel by one 8-bit value. This is
  an image-region comparison, not proof that every near/grazing pixel is exact.
- Ten 1080p hardware-headless render-only timings use two reversed-order
  repetitions and exclude 120 warmup frames. No solver, native window,
  encoding/readback or simultaneous GPU correctness job runs during timings.
  Current/filtered scenes pass the existing 8ms GPU / 4ms CPU-service budgets.

| View | Current GPU p95 | Filtered GPU p95 | CPU service p95, all controls |
|---|---:|---:|---:|
| Local mountain/32m | 2.904–2.913ms | 2.930–2.941ms | ≤0.391ms |
| Wide mountain/64m | 3.588–3.598ms | 3.625–3.629ms | ≤0.391ms |
| Wide uniform reference | 8.748–8.773ms | not tested | ≤0.391ms |

Matched filter-minus-current p95 differences are +0.026–0.041ms (about 0.7–1.2%).
These include run variance, not isolated shader attribution. The denser uniform
reference fails the 8ms scene gate, and does not remove distant grain; it is kept
as a diagnostic, not recommended as the production fix. Measurements are on
Starship's RTX 5070 Ti, not native monitor FPS or live-solver concurrency results.

The publisher verifies the delivered HTML bytes. The private headless Chrome
check validates comparison selection, filtering, unsupported-control disabling,
side swapping, finite-edge labels, relative evidence/image links, 390px layout
and the no-script fallback. It never attaches to the user's browser or desktop.
This is Starship browser-delivery evidence, not Snap network or native Cubey
acceptance. An earlier export's statistics link failed and was corrected in a
fresh immutable export; its diagnostic artifacts remain, rather than being
rewritten into a passing result.

## Reproduction and next boundary

Use fresh owned output leaves; each helper refuses old protocol/log/export names.
The baseline runtime and trial runtime are frozen separately. The baseline
harness refuses reuse against a changed runtime; the trial driver verifies its
own binary/SPIR-V/input identities and the baseline PNG identities.

```sh
rtk proxy python3 -B projects/terrain/study_wide_view_v1.py BASELINE --phase freeze
rtk proxy python3 -B projects/terrain/study_wide_view_v1.py BASELINE --phase statistics
rtk proxy python3 -B projects/terrain/study_wide_view_v1.py BASELINE --phase capture
rtk proxy python3 -B projects/terrain/review_wide_view_v1.py TRIAL --baseline BASELINE --phase freeze
rtk proxy python3 -B projects/terrain/review_wide_view_v1.py TRIAL --phase capture
rtk proxy python3 -B projects/terrain/review_wide_view_v1.py TRIAL --phase performance
rtk proxy python3 -B projects/terrain/report_wide_view_v1.py TRIAL --phase close
```

For report generation, run the exact `REQUIRED_TESTS` set in
`report_wide_view_v1.py` with CTest `-j1 --output-junit TRIAL/regression-tests.xml`,
then `report_wide_view_v1.py TRIAL --phase report`. Publish only its export leaf,
not the repository. Re-running the study with today's runtime measures the same
explicit variants; it does not recreate the older baseline binary identity.

The next bounded rendering investigation is distant **water** readability:
separate continuous coverage reconstruction from water normal/optics filtering
on these same saved states. Do not reduce rain to hide a rendering defect or
pretend broad storm sheets are narrow permanent streams. If this ground stencil
fails motion/seam review, the larger fallback is a real source-aligned filtered
height/normal hierarchy, not an unbounded increase in triangles. GUI review,
default scene promotion, seamless hydraulic continuation and push are deferred.
