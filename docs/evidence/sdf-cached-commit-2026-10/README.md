# Qualified Cached commit reuse

[Interactive HTML report](http://starship:8787/reports/3659235195747d263ebf8734145db49ba6c1bed67c875e6f62f63274e68d300f/).
[Results and next boundary](../../notes/sdf-cached-commit-results.md).

Bounded E0/E1 execution, plus the smallest E2 pruning comparison, from the
[research plan](../../notes/sdf-cache-commit-research-plan.md). Reuse passes raw
cache identity in the qualified cases and the proposed usefulness gates.
Regional pruning fails cache identity and remains unqualified. No persistent
allocator, time-sliced updater, editing UI or XR path is implemented.

## Source and controls

Intake `1f8bc565`; field implementation `207d2517`; main timing and native
sequences `22cc86b9`; retained-consumer verification `b8b17429`. Later commits
change harness diagnostics and verification without changing the cache-study
executable or four field kernels. Receipts record exact heads, source status,
binary/kernel/harness identities and commands. `baseline.json` preserves intake
source and shader hashes. All 206 original SPIR-V files and 940 protected
source/content files retain those hashes; the public executable is byte
identical to the saved intake control.

The raw root is `outputs/sdf/cached-commit-loop-20261030/`. Final native phases
are under `qualified-v2/`, except the first 22 inherited reuse cases. Inheritance
requires matching executable, shader and input hashes and preserves its original
receipt. A thin fixture initially violated the unchanged parser minimum and
was corrected before final qualification. Earlier pilots, incomplete runs and
rejected inputs remain in the raw root, excluded from accepted timings.

`raw-manifest.json` hashes actual local artifacts after all timing completes.
Large raw cache dumps and native PNGs remain local; the report bundles their
identities, complete measurement receipts and ten deduplicated native images.
`image-checks.json` proves decoded RGBA equality for all 80 source exports.
No videos or synthetic illustrations serve as native evidence.

## Reproduce serially

Use the retained release configuration and Vulkan validation environment. Run
one native job at a time; do not overlap builds, capture, analysis or encoding
with timed GPU work. `NEW` below is a fresh root. Each phase requires a fresh
output directory. The default baseline/public control paths point to the saved
raw root above; override `--baseline` with this packet's baseline and
`--public-binary` with an independently verified ordinary Cached executable
when reconstructing elsewhere. Compiler/driver changes require new identities
and qualification rather than inheriting these timings.

```sh
rtk proxy cmake --build build/release --target cubey_project_sdf_cached_commit_study cubey_project_sdf_cached_edit_study cubey_project_sdf_cached_commit_probe -j 6
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py prepare --output NEW/prepare
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py qualify --output NEW/reuse --inputs NEW/prepare/prepare.json --dump
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py qualify --output NEW/chains --inputs NEW/prepare/prepare.json --chains --dump
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py qualify --output NEW/pruned4 --inputs NEW/prepare/prepare.json --mode pruned --regions 4
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py faults --output NEW/faults --inputs NEW/prepare/prepare.json
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py sequences --output NEW/sequences --inputs NEW/prepare/prepare.json
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py retention --output NEW/retention-final --inputs NEW/sequences/sequences.json
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py timing --output NEW/timing --inputs NEW/prepare/prepare.json
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py timing --output NEW/pruned-cost-diagnostic --inputs NEW/prepare/prepare.json --diagnostic-pruning
```

For bounded A16 qualification, copy `prepare.json`, retain all 23 fixture rows,
and select only the four subject rows with `radius == 0.065`. The retained
subset input lives at `inputs/pruned16-subset.json` in this run and is
indexed by the raw manifest. Then run:

```sh
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py qualify --output NEW/pruned16 --inputs NEW/pruned16-subset.json --mode pruned --regions 16
rtk proxy python3 projects/sdf/tools/cached_commit_loop.py prune-probe --output NEW/prune-probe-raw --inputs NEW/prepare/prepare.json
```

Run the report builder only after native jobs finish. It expects the phase
layout under a `qualified-v2` child of `--raw`; use `NEW` as that child above.
The builder intentionally verifies the accepted case counts and identities,
hashes actual saved dumps, and rechecks every source image after lossless export.

```sh
rtk proxy outputs/sdf/quality-speed-loop-20261029/.venv/bin/python docs/evidence/sdf-cached-commit-2026-10/build_report.py --raw RAW_ROOT --export FRESH_EXPORT
rtk proxy node docs/evidence/sdf-cached-commit-2026-10/check_report.mjs FRESH_EXPORT
```

## Acceptance and limits

61 canonical/reuse pairs compare 2,953,737,728 stored samples, reachable topology
and logical leaves. Native scorers, independent byte comparisons and actual dump
hashes agree. The 35 fixture/radius cases include smooth bases with hard pending
tools, transforms, lattice/workspace boundaries, new/removed bricks and small
numeric values. Chains cover eight Guardian and sixteen Robot edits within
unchanged parser limits. Two forced-list-overflow controls take the canonical
full-build route. Eight negative requests preserve admission/base identity.

All 14 publication sequences retain source/history. Ten failure/retry sequences
retain the exact preview and pending state; a GPU frontier-capacity exception is
real, while other dispositions are serialized injections. Four normal retained
files replay through the original Cached consumer. All settled images match the
independent original-executable controls. There is no concurrent-input, GPU
preemption, physical display or VR acceptance claim.

The 48 matched timing runs retain 480 measured and 144 warmup commits. Complete
commit-to-ready includes study validation, allocation if selected, construction
and waits. Pooled and newly allocated fields are separate; the latter is not a
fresh-process startup. Rendering does not run concurrently during these builds.
Short rejected-pruning cost runs are excluded from the accepted speedup matrix.
Requested geometry/list/readback sizes are separate from allocator residency.

Pruning receipts deliberately retain failures. A4 passes small fixtures/Robot
but changes Guardian raw R16 bits. A16 changes Guardian leaf sets and Robot
samples. When logical leaves differ, native `r16_differences` is a legacy size
sentinel, not a matched-word error count; `summary.json` reports those counts as
null/unscored. Same-kernel float/packed-half/raw-image diagnostics investigate
selected failures. No point tolerance, geometry gate or sample resolution was
weakened to qualify a route.

Ordinary Cached is the quality control. Earlier sub-grid/grazing preview-to-bake
limitations and the unscored Guardian depth pixel remain unqualified. The cache
bit-identity evidence does not turn those into analytical-source passes.

`static-checks.json` covers eight cost views, eight image comparisons and all 66
sequence states in a mock DOM. `browser-checks.json` independently covers their
rendered controls, loaded comparison images and reset. Rapid sequence scrubbing
checks selected image paths/state; image decoding is checked separately in the
comparison views. Desktop layout screenshots are inspection evidence.
`delivery-checks.json` verifies all 26 immutable published files against
`report-manifest.json`; publication/delivery is separate from native acceptance.
`checks.json` summarizes scoped native, documentation, report and preservation
checks. The shared report server is left running. No push or project-site
deployment occurs.
