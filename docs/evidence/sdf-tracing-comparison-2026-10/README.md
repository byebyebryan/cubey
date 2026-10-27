# SDF tracing comparison evidence

Local source candidate: `cea1f1289173312e779783e4b51b6d38ed2a856e`.
Research/plan checkpoint: `720d60cb`. See the
[decision report](../../notes/sdf-tracing-comparison-results.md) and
[execution receipt](../../notes/sdf-tracing-comparison-progress.md).

This pack retains the bounded comparison, including target misses. It does not
adopt a default, publish new media or qualify a general editor.

## Inspectable receipts

| File | Scope |
| --- | --- |
| [summary.json](summary.json) | Source, binary, shader/input/raw-file hashes, resource/timing summaries and preservation checks |
| [qualification.json](qualification.json) | Complete nine-run matrix, 32 fixtures / 155 independent checks per candidate, seam/stencil probes, exact commands |
| [donor.json](donor.json) | Adapted pinned original compute core, first refinement; exact regions and conservative far bounds scored separately |
| [content-qualification.json](content-qualification.json) | Accepted Robot/Guardian differential full-source geometry checks; source hash, cache size, fallback count and construction cost |
| [benchmarks.json](benchmarks.json) | Resolution ladder, declared cost-guard skips, three interleaved 1080p runs for practical routes, completed GPU sample counts |
| [action-benchmarks.json](action-benchmarks.json) | Thirty runs across source/route/grid combinations, ten selected construction/backward/redo requests each |
| [Sampled action semantics](action-semantics-sampled.json), [analytic](action-semantics-analytic.json), [hybrid](action-semantics-hybrid.json) | Twenty-action hidden-source, undo/branch, appearance, pre-publication failure and retry checks |
| [native.json](native.json) and [logs](logs/) | Native capture/oracle/window commands and logs; final optical captures supersede the initial incorrect candidate optics variant |
| [optical-pixel-checks.json](optical-pixel-checks.json) | Glass transport is active in all three routes; zero magenta exhaustion pixels |
| [cli-preflight.json](cli-preflight.json) | Five unsupported route/study combinations reject before native resource initialization |
| [roi.json](roi.json) | Native shared-query normal images at fixed world scale, independent of the app orbit clamp |

Raw query results, per-frame CSV/trace files, exploratory versions and larger
action input snapshots remain under ignored
`outputs/sdf/tracing-comparison-20261026/`. Their hashes are retained in the
summary. Fixture/action inputs and executable tools are committed under
`projects/sdf/comparison/` and `projects/sdf/tools/`.

The final source-checkpoint query, frame and selected-action receipts share the
same binary SHA256. Each timing receipt retains its runner identity as well.
Native captures have separate commands/image hashes; the retained shader
identities distinguish runtime evidence from source and human acceptance.

## Selected images

| Subject | Sampled | Analytic / pruned | Hybrid / pruned |
| --- | --- | --- | --- |
| Native Robot | [image](images/robot-sampled.png) | [image](images/robot-analytic.png) | [image](images/robot-hybrid.png) |
| Native Guardian | [image](images/guardian-sampled.png) | [image](images/guardian-analytic.png) | [image](images/guardian-hybrid.png) |
| Native bounded glass | [image](images/glass-sampled.png) | [image](images/glass-analytic.png) | [image](images/glass-hybrid.png) |
| Half-grid sphere query ROI | [image](roi/subgrid-sphere-sampled-full.png) | [image](roi/subgrid-sphere-analytic-pruned.png) | [image](roi/subgrid-sphere-hybrid-pruned.png) |
| Thin-stick query ROI | [image](roi/thin-stick-0.5h-sampled-full.png) | [image](roi/thin-stick-0.5h-analytic-pruned.png) | [image](roi/thin-stick-0.5h-hybrid-pruned.png) |

ROI images are 128×128 orthographic GPU-query normal views over a `3 h0` world
viewport. They are not native PBR screenshots or a claim of additional authored
content. Native Guardian candidates were kept at 320×180 after the cost guard;
Robot/glass captures are 640×360. Full-HD timing claims apply only to measured
routes in the timing receipt.

## Reproduce

Build `cubey_project_sdf` and `cubey_project_sdf_region_tests` with the Release
preset. From the repository root, run:

```sh
python3 projects/sdf/tools/run_tracing_qualification.py --output outputs/sdf/tracing-recheck/queries
python3 projects/sdf/tools/qualify_content_queries.py --output outputs/sdf/tracing-recheck/content
python3 projects/sdf/tools/benchmark_tracing.py --full --output outputs/sdf/tracing-recheck/frames
python3 projects/sdf/tools/benchmark_tracing_actions.py --output outputs/sdf/tracing-recheck/actions
python3 projects/sdf/tools/qualify_tracing_native.py --output outputs/sdf/tracing-recheck/native
python3 projects/sdf/tools/check_tracing_images.py outputs/sdf/tracing-recheck/native
python3 projects/sdf/tools/capture_tracing_roi.py --output outputs/sdf/tracing-recheck/roi
ctest --test-dir build/release -R '^sdf_(region|recipe|authored|material|workshop|normal_projection|mirror_author)_tests$' --output-on-failure
```

Run GPU commands sequentially. The qualification commands require Vulkan validation.
`qualify_tracing_native.py` also requires Xvfb for its isolated virtual-window
smoke. It does not connect to a physical desktop. The donor is separately
reproduced with:

```sh
build/release/projects/sdf/sdf --headless --headless-render-only --require-validation --frames 1 --width 32 --height 32 --comparison-donor --comparison-input projects/sdf/comparison/fixtures.json --comparison-output outputs/sdf/tracing-recheck/donor.json
```

For the small action-semantic matrix, use `--comparison-actions
projects/sdf/comparison/actions.json`, `--authored-scene
projects/sdf/comparison/initial.json`, twenty headless frames and a
`--comparison-output` path. Select sampled/full, analytic/pruned or
hybrid/pruned explicitly. The larger content-action tool has its own generated
inputs and keeps the full-count transition separate from partial states.

The donor's MIT license, exact copied core, pinned revision and adapter scope
are retained in [the project-local donor directory](../../../projects/sdf/third_party/lipschitz_pruning/README.md).
