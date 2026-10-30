# Frozen-base live-edit native study

Bounded stages 1–2 of the continuous-editing plan. The results concern a private
renderer, frozen base plus canonical pending suffix, fresh-bake controls and
serial native motion. They do not qualify an updater, continuous input or VR.

[Published HTML report](http://starship:8787/reports/41999c42e4ba06abdfad59d6ef6b005ca14d98c5c63a9a377123eaa00cc60e51/).

The [execution record](../../notes/sdf-live-edit-experiment-progress.md) explains
the checkpoints, failed pilots and frozen gates. `summary.json` is the decision
data; `timing.json` retains all 108 native runs and their complete per-frame
GPU samples. `geometry.json` scores the unchanged independent source rules.
`qualification.json` records native-derived camera/shadow replay and point
checks. `postqualification.json` records compute-to-fragment validation,
full-image handoff comparisons and maximum-tail traversal checks. Flagged pixels
remain explicitly unscored. A failed approximation/handoff gate is not converted
into a pass by those exclusions.

Run native phases serially, before analysis or encoding:

```sh
rtk proxy cmake --build build/release --target cubey_project_sdf_live_edit_study -j 6
rtk proxy python3 projects/sdf/tools/live_edit_loop.py geometry --output NEW/geometry
rtk proxy python3 projects/sdf/tools/live_edit_loop.py prepare --output NEW/prepare
rtk proxy python3 projects/sdf/tools/live_edit_loop.py qualify --output NEW/qualify
rtk proxy python3 projects/sdf/tools/live_edit_loop.py captures --output NEW/captures
rtk proxy python3 projects/sdf/tools/live_edit_loop.py timing --output NEW/timing --inputs NEW/prepare/admission.json
```

The public control defaults to the preserved `baseline-sdf` executable. The
original public shader hashes must match `baseline.json`. All output directories
must be fresh; receipts retain exact commands, inputs, source/executable/private
shader identities and GPU/driver. The successful final raw set is
`outputs/sdf/live-edit-loop-20261030/qualified/`. `raw-manifest.json` records its
files and all hashes; large PNG/binary/frame/log artifacts stay in ignored
outputs. Failed pilots are separate and are not mixed into final statistics.

After timing, use the existing analysis environment (NumPy 2.5.4, SciPy 1.18.1,
Pillow 13.0.0), or an equivalent isolated environment:

```sh
rtk proxy outputs/sdf/quality-speed-loop-20261029/.venv/bin/python docs/evidence/sdf-live-edit-2026-10/postqualify.py --raw NEW --output POST
rtk proxy outputs/sdf/quality-speed-loop-20261029/.venv/bin/python docs/evidence/sdf-live-edit-2026-10/build_report.py --raw NEW --post POST --export EXPORT
rtk proxy node docs/evidence/sdf-live-edit-2026-10/check_report.mjs EXPORT
```

`build_report.py` checks source/binary/shader consistency across phases, all
timing repeats, the original 194 shaders and 567 protected files. It exports
only intended HTML/JSON/lossless WebP assets. The static checker exercises all
subject/route filters, image choices, 32 motion states, reset and play controls.
It does not establish rendered browser acceptance. Delivery checks independently
verify the published bytes.
