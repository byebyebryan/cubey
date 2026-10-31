# Local updates and eight pending edits: evidence

Native study on 2026-10-30 PDT, RTX 5070 Ti / driver 610.57.04.
Intake `d0a9744d`, branch `sdf-tracing-comparison`. Scope and stop gates are in
the [research plan](../../notes/sdf-local-edit-research-plan.md). Final results
are in [the results note](../../notes/sdf-local-edit-results.md).

This packet separates exact cache identity, ordinary Cached quality, serialized
prefix publication, unit calibration and service-rate acceptance. The first four
have native evidence. The Guardian cost screen stops the joint live service
experiment before interleaved full-frame or timestamped-input acceptance.

## Provenance

- `99545d05`: frozen scope and intake receipt.
- `092c01fc`: stable-slot local builder, raw copying and first locality qualification.
- `47dca866`: private eight-sphere interval preview and source admission.
- `87776586`: separately qualified root dominance.
- `d4b2a2c4`: batch cache comparison, dispatch slicing, expanded geometry and
  serialized four/four prefix qualification.
- `9fa49486`: equal endpoints/grazing roster and matched calibration series.
- `faed0dae`: restrict the inherited scalar `sequence` entrypoint to one pending
  stamp; eight-stamp accounting uses the explicit `prefix` entrypoint.
- `a3e6f24f`: replay four retained sources through the original Cached consumer.

Every cohort retains its source head/status, executable and kernel hashes.
Qualification occurred at several checkpoints; do not imply one executable hash
was used for every run. Local and dominance SPIR-V hashes remain unchanged after
their independent qualification. The final scalar-sequence guard changes no
measured rendering or construction branch. All 211 intake SPIR-V files and the
public `sdf` executable remain byte identical. Protected content is checked
against `baseline.json`, with the six explicitly scoped source entrypoints
excluded from the unchanged-content assertion.

Raw runs, binary cache dumps, native images and copied intake executables remain
under `outputs/sdf/local-edit-loop-20261030/`, outside Git. Committed receipts
contain exact commands, identities, native comparisons, all final timing samples,
work counts and dump hashes. `raw-manifest.json` hashes the small JSON/log evidence;
`cache-pairs.json` records the larger normalized dump identities. Report generation
rehashes every selected binary dump against the native receipt.

The initial `inputs/`/`local-v1/` series contains a rejected oversized canonical
base fixture. Corrected coverage is `inputs-v2/`, `local-v2/` and
`local-chains-v2/`. The first root-dominance pilot had a reversed union comparison,
which produced no add certificates; it retained correct canonical results and is
not used for acceleration measurements. Expanded near-workspace geometry then
exposed finite-difference normal blending at a clipping branch. The final private
sphere shader uses analytic sphere/face normals and raw cutters. The failed
geometry receipt remains in `stroke-geometry-v2/`; final controls use
`stroke-inputs-v3/` and `stroke-geometry-v4/`.

The first radius-.15 timing cohort is retained but excluded: a short, separate
Python report syntax check ran during it. The selected `timing-15-v2/` cohort
repeats the complete matched series after that check. No C++ build, bulk analysis,
encoder or other native GPU job overlaps selected timing. Routine documentation
editing and read-only status/progress inspection continue during long jobs.

## Reproduction

Use an already configured Release build with the SDF private study targets.
These commands reproduce into fresh output directories in the retained study
workspace. The intake binaries/receipt and earlier fixture manifest referenced
by the harness must be present; they are intentionally hash-frozen. A clean
checkout on a different toolchain or GPU needs a new intake and fresh controls,
not edited hashes in the old evidence. The committed packet is independently
inspectable without those larger raw artifacts.
Keep native GPU jobs serial; do not overlap owned builds, analysis or encoding
with timing. Boundary GPU observations are retained, not exclusive-GPU proof.
Use fresh output directories: harnesses reject an existing directory.

```sh
rtk proxy cmake --build build/release --target cubey_project_sdf_cached_commit_study cubey_project_sdf_cached_stroke_study -j 6
rtk proxy python3 projects/sdf/tools/local_edit_loop.py inputs --output FRESH_LOCAL_INPUTS
rtk proxy python3 projects/sdf/tools/local_edit_loop.py qualify --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_LOCAL --mode local --dump
rtk proxy python3 projects/sdf/tools/local_edit_loop.py qualify --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_LOCAL_CHAINS --mode local --chains --dump
rtk proxy python3 projects/sdf/tools/local_edit_loop.py qualify --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_DOM --mode local-dom --dump
rtk proxy python3 projects/sdf/tools/local_edit_loop.py qualify --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_DOM_CHAINS --mode local-dom --chains --dump
rtk proxy python3 projects/sdf/tools/local_edit_loop.py copies --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_COPIES
rtk proxy python3 projects/sdf/tools/local_edit_loop.py timing --inputs FRESH_LOCAL_INPUTS/inputs.json --output FRESH_TIMING --radius .065
rtk proxy python3 projects/sdf/tools/stroke_loop.py inputs --output FRESH_STROKE_INPUTS
rtk proxy python3 projects/sdf/tools/stroke_loop.py admission --output FRESH_ADMISSION
rtk proxy python3 projects/sdf/tools/stroke_loop.py geometry --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_GEOMETRY
rtk proxy python3 projects/sdf/tools/stroke_loop.py replay --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_REPLAY
rtk proxy python3 projects/sdf/tools/stroke_loop.py caches --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_BATCHES --measure
rtk proxy python3 projects/sdf/tools/stroke_loop.py caches --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_SLICED --groups 32
rtk proxy python3 projects/sdf/tools/stroke_loop.py calibrate --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_CALIBRATION
rtk proxy python3 projects/sdf/tools/stroke_loop.py prefixes --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_PREFIXES
rtk proxy python3 projects/sdf/tools/stroke_loop.py retention --inputs FRESH_PREFIXES/prefixes.json --output FRESH_RETENTION
rtk proxy python3 projects/sdf/tools/stroke_loop.py captures --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_CAPTURES
rtk proxy python3 projects/sdf/tools/stroke_loop.py timing --inputs FRESH_STROKE_INPUTS/inputs.json --output FRESH_RENDER_TIMING
```

Run commit timing separately at `.02`, `.065` and `.15`. Defaults compare full,
global reuse, local and local + dominance, with three interleaved repeats of
three warmups and ten measured commits. Calibration compares whole dispatches
and 32/128/512/2048 groups, with the same repetitions. Positive slice sizes use
16 MiB raw-copy slabs. Preview timing compares original Cached, new zero,
original one-edit and 1/2/4/8-stamp preview routes at 1080p SS1; each row has
three interleaved repeats of 30 warmup and 150 measured frames.

The helper uses
[`vkCmdDispatchBase`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatchBase.html)
with `VK_PIPELINE_CREATE_DISPATCH_BASE_BIT` and the identical qualified kernels.
Shader counts and row strides stay fixed; offsets select bounded chunks within
a dispatch row. Separate descriptor/pipeline resources belong to the private
helper. Cooperative callbacks execute outside the GPU-owner submission and
expose measured work units; a live frame scheduler is not implemented here.

## Resource and failure contracts

The native hosts retain two complete Fields. Each Field has its existing paired
physical versions; there is no third complete Field. Stable physical slots are
private metadata in one `CachedCommitStudy` instance, keyed by Field identity and
geometry hash. Keep one helper for the lifetime of a local chain; an unknown
noncompact Field from a different helper is outside this API contract. A fresh
canonical build resets to compact allocation. Removed slots leave holes, new
slots append to a bounded high-water mark, and no reclamation is added.

The final unit/working-resource checks cover requested geometry/list storage and
the largest diagnostic staging allocation. They are not allocator residency or
complete-renderer memory. The copied candidate always transfers 345,131,300 bytes;
locality reduces evaluation and sample writes, not this initial copy volume.

The numerical sample enclosure includes `2R + tau`, two authored-evaluation
budgets, an R16 allowance and a separate brush-evaluation guard. These are native
engineering guards qualified on this bounded roster, not a universal IEEE proof.
Old reachable subtrees can be skipped; children under old uniform ancestors are
initialized as exposed even if outside the geometric band. Canonical sampling
handles uncertain/new bricks and zero/subnormal/nonfinite old-sample results.

Frontier and append tests lower a private candidate's limit to force a real GPU
capacity exception. Allocation refusal and cancellation are disposition
injections, not an OS/GPU OOM or preemption test. Stale/discard cases finish GPU
work and reject publication. Each failed attempt preserves the old preview and
eight pending IDs. All frames are completed/read back before descriptor changes;
this does not establish in-flight lifetime safety. The scalar inherited
`sequence` mode accepts one edit; the explicit `prefix` mode qualifies four/four.

The eight slots include the frozen prefix. Source/history/material serialization
and the 1024-leaf/2048-instruction/depth-24/stack-32 limits remain unchanged.
No long-session capacity, general domain/smoothing editor, mixed-operation stroke,
brush/undo UI or physical VR acceptance is claimed. Ordinary Cached sub-grid and
grazing changes and the previously unscored Guardian depth pixel remain separate.

## Report and checks

After all timed jobs finish:

```sh
rtk proxy python3 docs/evidence/sdf-local-edit-2026-10/build_report.py --raw outputs/sdf/local-edit-loop-20261030 --export FRESH_EXPORT
rtk proxy node docs/evidence/sdf-local-edit-2026-10/check_report.mjs FRESH_EXPORT
```

The export contains only the report, data and byte-preserved native images.
Static mock-DOM checks, browser screenshots/interactions, HTTP byte verification,
publication receipt and focused repository checks have separate receipts.
Publishing this inspectable report is not a project-site deployment.
