# SDF source shader selection goal loop

Date: 2026-10-29 UTC (2026-10-28 local). Baseline: `797411d`.
Branch: `sdf-tracing-comparison`, initially clean.

## Scope and contract

Turn the [Guardian shader finding](sdf-guardian-frame-profile-results.md) into
safe automatic selection for already opt-in authored source tracing. Preserve
the sampled/full default, existing debug/material/optical semantics, authored
source/history formats, tolerances, and protected content. Keep work local.

The source route owns three prebuilt compatible pipelines per renderer lifetime:
general/debug, lit/general-material and lit/eligible-one-material. Sampled and
retained diagnostic routes create only their original pipeline. No per-frame,
appearance-edit or undo pipeline compilation is needed.

The lit shader removes debug branches. Its material specialization is a core
Vulkan specialization constant, set only when the published scene has exactly
one material and the existing material fast path is enabled. Otherwise it keeps
the general evaluator. Debug views always use the complete original shader.
Material domains, patterns, bump, cut ownership and glass interface queries
retain their existing functions. Lighting/extent/descriptor-family changes
remain renderer recreation boundaries.

Selection uses published appearance metadata plus the current frame's debug
and fast-path settings. It must never select a one-material pipeline using a
pending/unpublished source state. Existing publication waits and failure/retry
semantics remain; all pipelines are prepared before a renderer becomes usable.

## Checkpoints and acceptance

1. Commit this contract; save current binary/shader/content hashes.
2. Add a project-local selection function and meaningful boundary tests; create
   lit shader wrappers and compatible prebuilt pipelines through existing APIs.
   Keep a `source-shaders=general` measurement/fallback control.
3. Exercise one/multiple/empty material eligibility, fast-path and debug changes,
   domains/pattern edits, undo/redo/branching, retained snapshots on injected
   failure and retry. Assert geometry-cache reuse for appearance-only changes.
4. Qualify Guardian, multi-material Robot and glass; compare general/automatic
   captures at matching camera/settings and expose all differences. Retain
   frozen independent geometry checks and debug/material/transport references.
   Reuse existing numerical contracts; do not relax them to admit an image.
5. Test a real windowed lifecycle under the existing native harness. Headless
   capture and virtual-window evidence remain separate from physical acceptance.
6. Measure current general/automatic source routes with three interleaved
   repetitions, 180 frames/30 warmup/150 complete GPU samples, fixed views and
   recorded GPU load. Report setup cost separately. No simultaneous owned
   build/test/capture jobs during timings.
7. Commit evidence and a decision report. Preserve historical shader binaries
   and default allocation behavior. Stop before finer CSG regions, GPU building,
   a new editor, public media, push or default-tracer adoption.

Raw evidence: `outputs/sdf/shader-selection-20261029/`.
