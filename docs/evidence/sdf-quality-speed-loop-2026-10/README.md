# SDF quality/speed loop evidence

Date: 2026-10-29. [Results and decision](../../notes/sdf-quality-speed-results.md) ·
[plan](../../notes/sdf-quality-speed-research-plan.md).

The useful result is an explicit private preview compromise: refine cached
primary hits, optionally evaluate source CSG normals at those hits, retain
cached shadows. It meets the proposed frame budget in the tested views but
fails the detail-preserving profile. Missing features and wrong-front-surface
cases remain. Sampled/full stays the default.

## Receipts

- [Summary, decisions and gates](summary.json), with complete quality tails,
  frame ranges, publication stop-rule calculations and resource bounds.
- [126 core timing runs](timing-core.json) and
  [48 close/distant timing runs](timing-extended.json): complete raw pass arrays,
  frame indices, native argv, resources and GPU boundary observations.
- [50 final-binary capture identities](captures.json) and
  [40 source-relative quality comparisons](quality.json).
- [384 native-validated pose frames](poses.json),
  [per-pose comparisons](temporal.json) and [independent fixture coverage](fixture-aa.json).
- [Canonical/history/recovery actions](history.json) and
  [24 full-scene publication runs / 168 actions](publication.json).
- [Frozen source and explicit approximate scores](contracts.json),
  [full source fixtures](source-fixtures.json),
  [selected approximate fixtures](selected-fixtures.json), and native query
  answers for [source](queries-source.json), [S](queries-sampled.json),
  [C](queries-refine03125.json), [N](queries-normal.json), and
  [C+N](queries-combined.json).
- [Controlled actual GPU refinement helper](cell-oracle.json): 22 observations
  and 2,560 independent gradient checks. This proves the stated controlled
  quantized-cell cases, not equivalence to arbitrary source CSG geometry.
- [ROI query images and feature counts](roi.json),
  [matched native shadow-off captures](shadow-ablation.json) and
  [short hybrid context pilot](hybrid-context.json).
- [Compiler executable statistics](compiler.json), including the unusable
  driver local-memory values and their exclusion.
- [Ten actual admission rejections](negative.json),
  [baseline source/SPV hashes](baseline.json),
  [preservation check](preservation.json), [focused checks](checks.log),
  [report checks](report-checks.json) and [content hashes](manifest.json).

Final timing and static quality use native binary SHA-256
`1c71ec2622022f2f56cd85a9e19b80b7290bc35c5b9408ac193fd1cd55e1f587`.
The implementation commit is `1b4faefb`. Each packet retains its own binary,
source and tool identities: earlier history/publication/contract/compiler
observations are supporting evidence, not relabeled final-binary observations.
The subsequent native changes fix private diagnostic descriptor/layout and UV
resource lifetime. All 166 original SPVs remain byte-identical.

Final frame timing observes encoder utilization at zero in all 348 run
boundaries, on an uncontrolled shared host. Publication observes 0–50% encoder
load. There is no continuous GPU exclusivity or interactive frame-rate claim.
No owned build/test/capture/encoding overlaps the final timing batch.

Native diagnostic discrepancies remain unscored with unchanged depth/normal
limits; silhouette statistics additionally exclude two-pixel neighborhoods.
No image goldens were refreshed. Full native frames, masks, sidecars, logs,
CSVs, action manifests and excluded failed attempts remain in
`outputs/sdf/quality-speed-loop-20261029/`. Geometry sidecars are not vendored.
The published report bundles selected native images, crops and review clips.

## Reproduction

Build `cubey_project_sdf` and `cubey_project_sdf_cached_cell_contract` in the
release tree. Run Vulkan work serially on the recorded GPU/driver. Output
directories must be fresh. The runner defaults to both Guardian and Robot.
It records exact native argv and verifies sample coverage instead of trusting
aggregate profiler output.

```sh
python3 projects/sdf/tools/quality_speed_loop.py timing --output outputs/sdf/repeat-core --routes sampled source adaptive refine03125 normal normal-zero combined --views preset side rear
python3 projects/sdf/tools/quality_speed_loop.py timing --output outputs/sdf/repeat-extended --routes sampled source refine03125 combined --views close distant
python3 projects/sdf/tools/quality_speed_loop.py capture --output outputs/sdf/repeat-captures --routes sampled source refine03125 normal combined --views preset side rear close distant --probe
python3 projects/sdf/tools/quality_speed_loop.py contracts --output outputs/sdf/repeat-contracts --routes sampled source refine03125 normal combined
python3 projects/sdf/tools/quality_speed_loop.py sequence --output outputs/sdf/repeat-sequences --routes sampled source refine03125 combined
python3 projects/sdf/tools/quality_speed_loop.py sequence --output outputs/sdf/repeat-pose-geometry --input outputs/sdf/repeat-sequences --routes sampled source refine03125 combined --probe
python3 projects/sdf/tools/quality_speed_loop.py history --output outputs/sdf/repeat-history --routes sampled source refine03125 normal combined
python3 projects/sdf/tools/quality_speed_loop.py publication --output outputs/sdf/repeat-publication --routes sampled source refine03125 combined
python3 projects/sdf/tools/quality_speed_loop.py negative --output outputs/sdf/repeat-negative
```

Analysis/capture parity uses Python 3.13.14, numpy 2.5.4, scipy 1.18.1 and
Pillow 13.0.0 in the isolated raw-output `.venv`; CPU review clips use FFmpeg
libx264. Timing and non-image fixture/history phases use Python stdlib.
`quality_speed_analysis.py` scores static captures.
`quality_speed_review.py --native` scores the explicit pose snapshots, with
the documented raw-root layout. `assemble_quality_speed_results.py` verifies
coverage/identities, archives receipts and creates the report bundle from a
completed study. It performs CPU image/video work after timing has finished.

To inspect the selected candidate directly:

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-limestone.json --headless --require-validation --motion static --frames 1 --width 1920 --height 1080 --supersample 1 --surface-trace sampled --csg-evaluation full --region-axis 64 --cached-quality refine-normal --shadows 1 --no-ao --no-ground --no-clouds --no-occluder --output outputs/sdf/guardian-cached-preview.png
```

Private candidates reject windowed/nonstatic and unqualified combinations.
They are not a product default or a windowed runtime qualification.
