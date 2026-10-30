# SDF render-mode promotion execution

Started 2026-10-29 from `a9ab2976`, with a clean worktree on
`sdf-tracing-comparison`. The [plan](sdf-render-mode-promotion-plan.md) owns the
scope. Raw evidence: `outputs/sdf/render-mode-loop-20261029/`.

## Initial checkpoint

The baseline preserves 192 existing SPVs and the prior native executable.
The first change admits only uninstrumented SS2 cached-quality studies;
headless/static/lighting and all geometry-diagnostic limits remain intact.
No product mode/default, authored/history/builder or shared-engine change.

The bounded sampling matrix contains 32 configurations, three serial
interleaved repetitions, followed by 32 native captures. Each timing run uses
180 frames, 30 warmup and 150 verified samples per GPU pass. Owned GPU jobs,
builds, tests and encoding stay out of the timed batch. GPU load is observed
at run boundaries; this is not a continuous exclusivity claim.

## Sampling checkpoint

All 96 timed runs and 32 uninstrumented captures are complete. Guardian preset
C+N is 4.661–4.719 ms at SS1 and 12.046–12.194 ms at SS2; close is
8.917–8.978 / 25.824–26.162 ms. Robot preset is 4.322–4.360 / 12.179–12.224 ms;
close is 7.646–7.695 / 30.011–30.075 ms. SS2 is a real extra cost, and its close
views do not meet the preferred 16.7/20 ms lines. Existing sample settings are
retained while the mode itself is promoted; SS1 remains independently selectable.

## Runtime candidate

Implemented project-local named mode resolution, optional legacy overrides and
null template bindings. The native panel provides Rendering and Supersampling
selectors. Mode/extent preparation retains the old renderer on failure, reuses
compatible field/region caches and releases the field for analytical-only use.
The default resolver remains Cached during qualification.

Twenty paired alias cases (five modes, both subjects and sample settings) match
their explicit legacy routes exactly. All twenty directed small-scene mode
transitions and five injected preparation/activation failures pass, including
settled image identity, retry, field reuse and old-mode retention.

Five nine-action history/recovery replays pass. Forty native captures cover
AO, soft shadows, ground/occlusion, wood/metal, furnace, clear/frosted/clouded
glass, lighting cycles, construction/presentation and debug views. The RGB
diagnostic-color screen passes; it is not complete floating-point instrumentation.

Owned Xvfb checks pass actual mode/sample selection, two resizes without a
geometry rebuild, orbit/zoom/reset, normal-debug round trip, rejected/retried
owned reload, a material edit without field rebuilding and authored history
seeking/undo/redo/playback. These are native virtual-window checks, separate
from physical-desktop and human visual acceptance. The old harness now honors
preset supersampling; its sixty-pixel header offset was inspected in native
captures. An initial inspection tried Escape to close a popup; the app's
ordinary Escape-to-exit behavior closed the window, so inspection uses a panel
background click instead. That attempt is not a passed interaction receipt.

Final public-route timing/publication, default promotion and delivery remain
pending. The 192 preexisting SPVs remain byte-identical; two new wrappers add
the same cached policy to material lighting without changing transport code.
