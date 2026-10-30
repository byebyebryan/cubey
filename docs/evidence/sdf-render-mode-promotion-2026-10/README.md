# SDF render-mode promotion evidence

Date: 2026-10-29. [Results](../../notes/sdf-render-mode-promotion-results.md) ·
[plan](../../notes/sdf-render-mode-promotion-plan.md).

Balanced is the authored/workshop default, with independent preserved sample
settings. All five rendering choices remain available. Source atmosphere uses
the newly qualified public root-far route; other source study controls retain
their earlier admission. No shared engine or canonical geometry/builder change.

## Receipts

- [Summary and gates](summary.json), [96-run sampling study](sampling.json),
  [108 final public-route runs](timing.json), complete raw pass samples and
  native commands. The [stopped A16 investigation](a16-investigation.json)
  explains the source backend change and is excluded from final acceptance.
- [Twenty alias pairs](aliases.json), [default equivalence](defaults.json),
  [twenty directed transitions and injected failures](transitions.json),
  [full-scene round trips](large-transitions.json), [working material retention](working-material.json).
- [Five history replays](history.json), [feature captures](features.json),
  [native diagnostic-color screen](native-color-screen.json),
  [155 frozen source checks](contracts.json), [eleven actual rejections](negative.json).
- [Actual native mode/sample selectors](window.json),
  Balanced [reload/material](balanced-window-reload.json) and
  [history](balanced-window-history.json), Analytical root-far
  [reload/material](analytic-window-reload.json) and
  [history](analytic-window-history.json), and [workshop controls](workshop-window.json).
- [Full-scene publication](publication.json), [forty native frame identities](captures.json),
  [ninety-six native arc frames](motion.json), [baseline](baseline.json),
  [source/SPV preservation and final identity](identities.json).
- [Focused checks](checks.log), [report validation](report-checks.json),
  and [archive hashes](manifest.json).

The final binary is
`a00dfa2527febd2e81984a56980b929be86b19abe4a5d21b9336f63dd28ba98e`.
Earlier feature and Balanced-window supporting receipts retain their own binary
identities. Subsequent native changes add metrics and promote the default/public
analytical backend; cached helpers and all 192 earlier SPVs remain unchanged.

Raw logs, CSVs, images, owned test models/configs, action manifests and excluded
attempts remain under `outputs/sdf/render-mode-loop-20261029/`. Full native
frames, UI images and review clips are bundled in the report. No geometry
sidecars or refreshed goldens are needed for the newly admitted SS2 image/time
study. Diagnostic limits and approximate/source profiles remain distinct.

## Reproduction

Build `cubey_project_sdf`, `cubey_project_sdf_render_mode_tests` and existing SDF
tests in the release tree. Native GPU jobs must run serially, with owned builds,
tests, capture and encoding outside timing/publication. Fresh output directories
are required. Exact commands/configuration/backend/sample settings are in each
receipt; the runner defaults to both subjects.

```sh
python3 projects/sdf/tools/render_mode_loop.py sampling --output outputs/sdf/repeat-sampling
python3 projects/sdf/tools/render_mode_loop.py aliases --output outputs/sdf/repeat-aliases
python3 projects/sdf/tools/render_mode_loop.py transitions --output outputs/sdf/repeat-transitions
python3 projects/sdf/tools/render_mode_loop.py history --output outputs/sdf/repeat-history
python3 projects/sdf/tools/render_mode_loop.py runtime_timing --output outputs/sdf/repeat-runtime
python3 projects/sdf/tools/render_mode_loop.py publication --output outputs/sdf/repeat-publication
python3 projects/sdf/tools/render_mode_loop.py contracts --output outputs/sdf/repeat-contracts
python3 projects/sdf/tools/render_mode_loop.py negative --output outputs/sdf/repeat-negative
xvfb-run -a -s '-screen 0 1280x1100x24' env CUBEY_SDF_PRIVATE_XVFB=1 python3 projects/sdf/tools/check_render_modes_window.py --config projects/sdf/presets/survey-robot-fieldkit.json --initial-mode auto --output outputs/sdf/repeat-window
```

Feature screening uses the isolated analysis environment retained under the
earlier quality/speed raw root (Python 3.13.14, numpy 2.5.4, scipy 1.18.1,
Pillow 13.0.0). CPU clips use FFmpeg/libx264 after native timing/capture.
`assemble_render_modes_results.py` validates frame coverage, identities and
publication gates before archiving a completed study and building its report.
Virtual-window acceptance, HTTP delivery and human/physical acceptance are
separate facts.
