# Ray tracing optics evidence

Date: 2026-11-05. Runtime `dd432598`; branch `raytracing-first-pass-20261104`.
See the [results](../../notes/raytracing-optics-results.md),
[fixed plan](../../notes/raytracing-optics-plan.md) and
[operational guide](../../../projects/raytracing/README.md).

This directory retains compact source/asset identities, numerical, convergence,
native, regression and cost receipts. Complete native captures, video, raw GPU
observations, commands and logs remain in ignored `outputs/raytracing/optics/`.
The report bundles selected native media and receipts without the GLB or donor
source/build tree. The original first-pass evidence and SDF/horse work remain
separate and preserved. Report publication is separate from source push, merge
or public demo-site promotion.

The normal dev suite passes 279/279 selected groups, and focused RT sync tests
pass 10/10. A broader sync-enabled dev run retains four reused-depth hazards,
all independently reproduced in the original build; it is not a global sync
pass. Physical desktop interaction, browser rendering and human artistic
acceptance retain separate boundaries. The timing screen passes for both frozen and moving-sun scenes.


| Receipt | Scope |
| --- | --- |
| [Build](build.json) | Actual compiler, optimization flags and default-off studies |
| [Identity](identity.json) | Runtime and timing source, exact binary/SPIR-V/model/preset hashes; quality inputs match |
| [Reference](reference.json) | Unmodified pinned Khronos reflection donor, native capture and validation |
| [Geometry](geometry.json), [optics](optics.json) | Independent CPU geometry/interface comparison and fixed tolerances |
| [Sky lookup](sky-lookup.json) | Five independently aimed primary/secondary environment comparisons |
| [Convergence](convergence.json), [residuals](residuals.json) | Full 1080p default/control HDR errors/status counts; separate bounded discarded-throughput diagnostics and explicit budget exhaustion |
| [Native](native.json) | Owned Xvfb interaction/save/resize, sky refresh, unsupported camera rejection and deterministic video receipts |
| [Regressions](regressions.json) | Legacy RT and shared SDF/PBR/Atmosphere parity; saved-window reload within one RGB8 level |
| [Tests](tests.json) | 279/279 normal dev, 10/10 focused RT sync, four reproduced pre-existing global sync failures |
| [Timing summary](timing-summary.json), [frozen repetitions](timing.json), [refresh repetitions](refresh-timing.json) | 1080p three serial repetitions, completed-sample scopes and retained outliers |
| [Startup and memory](startup-memory.json) | Single validated reflection still; requested, allocated and process-observed scopes kept separate |
| [Media](media.json), [retention](retention.json) | Retained media identities; all 11 protected original SDF/horse files unchanged |

The native media is authored in `outputs/raytracing/optics/quality-final/`,
`reflection-orbit.mp4`, `low-sun.png`, `sunset.png` and `reference/reflection.png`.
Earlier failed/development packets remain retained and are not relabeled as
final qualification. The original worktree stays on `sdf-tracing-comparison`;
the isolated implementation branch holds local commits only.
