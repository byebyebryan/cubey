# Cached default and one-operation preview

[Interactive HTML report](http://starship:8787/reports/7052366b02e5c143cbc8634222fd59a5fb67d3970dbf4a8cf15d2d1edd78775e/).
The final snapshot includes depth-error tails and grazing-ray diagnostics.

Bounded follow-up to the general live-CSG experiment. The user selected Cached
as default and a single pending add/cut, committed before accepting another.
Canonical source/history is retained separately from the live render operation.
There is no public brush UI, background updater or continuous-input/VR claim.

Runtime implementation: `e5bc3628`. Visibility-qualified harness:
`1a498e7c`. The qualified raw set is
`outputs/sdf/cached-one-edit-loop-20261030/qualified-visible/`.
`baseline.json` in its parent preserves the original executable, 202 shader
hashes and 567 protected source/content hashes. `raw-manifest.json` indexes the
large ignored artifacts; this packet retains complete measurement receipts.

The first cut placements had no meaningful visible effect and were rejected
after image review. Their raw `final/` folder and earlier layout pilots remain
separate. Qualified cuts are centered on measured visible Cached hits;
`visible-placement.json` retains the reference rays and hashes. Every accepted
subject/view/operation changes at least ten 160x90 control rays by classification
or more than one voxel in depth before timing begins.

Run native phases serially, before analysis/encoding:

```sh
rtk proxy cmake --build build/release --target cubey_project_sdf_cached_edit_study cubey_project_sdf_render_mode_tests cubey_project_sdf_shader_selection_tests -j 6
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py prepare --output NEW/prepare
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py geometry --output NEW/geometry
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py defaults --output NEW/defaults --inputs NEW/prepare/admission.json
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py replay --output NEW/replay --inputs NEW/prepare/admission.json
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py captures --output NEW/captures --inputs NEW/prepare/admission.json
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py sequences --output NEW/sequences --inputs NEW/prepare/admission.json
rtk proxy python3 projects/sdf/tools/cached_edit_loop.py timing --output NEW/timing --inputs NEW/prepare/admission.json
```

All output directories must be fresh. The default public control is the preserved
`baseline-sdf` executable, always explicitly selecting Cached. Current public
defaults are checked independently at SS1/SS2. Owned Xvfb checks exercise Default
resolving to Cached, all five mode options, sample/resize controls, and workshop
material/specimen changes; those are virtual native windows, not human desktop
acceptance. Their source receipts live in the raw parent `window-*` directories.

After GPU timing, use the existing isolated NumPy/SciPy/Pillow environment:

```sh
rtk proxy outputs/sdf/quality-speed-loop-20261029/.venv/bin/python docs/evidence/sdf-cached-one-edit-2026-10/build_report.py --raw NEW --export EXPORT
rtk proxy node docs/evidence/sdf-cached-one-edit-2026-10/check_report.mjs EXPORT
```

The builder verifies all phase identities, 24 groups/72 runs/10,800 measured
frames, repeated frame coverage, old shader/protected hashes, geometry image
payloads and lossless native image exports. Commit measurements use 960x540 SS1
sequences with unchanged 3D cache resolution; rendering timings use 1080p SS1.
Renderer creation is prewarmed and separately recorded. Candidate allocation,
full field construction, publication and replacement-frame completion are
separate quantities. GPU render time excludes the blocking commit.

`geometry.json` uses frozen cached-profile limits (half-voxel regular depth,
two-degree normals away from seams), with sub-grid/tangent diagnostics separate.
The earlier analytical-source gate is unchanged. Native validation flags are
excluded and counted; exclusion does not certify those pixels. A tiny pending
analytical cut can disappear after ordinary sampling and is explicitly reported.

`sequences.json` proves success, failed candidate retention, explicit retry,
retirement after replacement-frame completion, retained canonical source/history
and byte-identical settled images against original-executable Cached renders.
The static report checker covers every image pair and all 36 recorded sequence
states. Browser and HTTP-byte checks are separate delivery evidence.

`browser-checks.json` records the final report's rendered controls, loaded
images, failure/retry state and reset. `report-browser.png` preserves the
reviewed desktop layout. `delivery-checks.json` verifies all 81 published assets
against `report-manifest.json`. `checks.json` summarizes the scoped native,
static and delivery checks and their acceptance limits.
