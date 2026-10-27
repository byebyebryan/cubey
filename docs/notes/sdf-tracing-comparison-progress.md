# SDF tracing comparison progress

Local implementation branch: `sdf-tracing-comparison`. Research checkpoint:
`720d60cb`. Contract: [comparison plan](sdf-tracing-comparison-plan.md).

The loop is authorized for local implementation and qualification. The sampled
default, accepted content and source formats stay fixed. No push or publication
is authorized. Scripted actions are the initial interaction scope.

| Gate | State | Evidence / next check |
| --- | --- | --- |
| M0 | Bounded contract qualified | 32 fixtures. Revision 1 froze 142 checks; explicit revision 2 adds 13 near-parallel/inside-clip rays without changing tolerances. Final SHA256 `d7089102beeb51a1edcf1176fb7d09dabfd12c2892832483f1ca05280e48e9d3`. Separate 20-action manifest. |
| M1 | Passed bounded controls | A0/H0 pass all 155 independent checks; native analytic rendering allocates zero field bytes. All 29 baseline shader binaries match. |
| M2 | Passed bounded controls | Adapted donor first refinement: 53,784 exact and 1,512 far-bound point checks. CPU exact region builder and GPU A1/H1 pass at axes 4/16/64, including seam/stencil probes and explicit fallback. This is not universal FP32 certification. |
| M3 | Study implemented | Native A1/H1 use the same pruner. Twenty-action undo/redo/publication replay passes. Selected Robot/Guardian states have three interleaved latency runs per route. CPU construction cost is included. Coarser hybrid cache deferred at the failed Guardian scaling gate. |
| M4 | Bounded comparison complete | [Decision report](sdf-tracing-comparison-results.md) and [committed evidence pack](../evidence/sdf-tracing-comparison-2026-10/README.md). Robot has three 1080p timing runs per practical route; Guardian candidates stop at the predeclared 320p cost guard. Native/glass/virtual-window scopes pass. Default adoption and physical/user acceptance remain separate. |

This is an execution receipt, not a claim of candidate acceptance. Record failed
gates and unresolved cases here rather than silently changing their contracts.

The first 8M-reference cap exposed Guardian fallback. The final selected-study
cap is 32M 32-bit references (128 MiB); measured final content has zero fallback.
An initial instruction-count arithmetic guard was replaced by a dependency-path
guard: hard min/max does not accumulate rounding, smooth operators do. These
are explicit candidate revisions; raw preflight evidence remains under ignored
`outputs/sdf/tracing-comparison-20261026`.

Review caught a missing `SDF_OPTICS` define in candidate material entrypoints.
It was corrected before optical acceptance, then glass and transport diagnostics
were recaptured. The original opaque candidate captures are not optical evidence.
