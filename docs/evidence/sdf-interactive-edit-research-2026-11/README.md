# Edit service solution research evidence

This packet supports the [solution review](../../notes/sdf-interactive-edit-solution-research.md).
Source intake is `6e9bce4e`; the retained native runtime is `f9c3daa4`.
This pass does not change runtime, shaders, authored content, defaults or
capacity, and does not execute new GPU benchmarks or publish a report/site.

- [analysis.json](analysis.json) records input hashes, cohort arithmetic,
  optimistic sequential window replays, a scalar-validity counterexample and a
  dense and classification-only center-cache memory screens.
- [analyze.py](analyze.py) reproduces that analysis using Python's standard
  library and the existing October 31 receipts. It never launches the native
  executable. Run `python3 -B docs/evidence/sdf-interactive-edit-research-2026-11/analyze.py`.
- [fidget-reference.json](fidget-reference.json) pins the inspected primary GPU
  source to commit `25528e0c2c5251f3b7538b2d43968a0fe5d718c8` and its content hash.
- [sources.json](sources.json) records primary references, scope and limitations.
- [source-audit.json](source-audit.json) fingerprints the inspected runtime and
  completion/lifetime interfaces; runtime changes and new GPU runs are zero.

The native inputs remain under
`outputs/sdf/interactive-edit-loop-20261031/qualified-v1/`:
`service/service.json` and `post-controls/controls.json`. Their full protection
policy is in the [previous retention manifest](../sdf-interactive-edit-2026-10/retention.json).
No old evidence was deleted or regenerated.

Offline packing assumes no CPU readiness gaps, callback/profiling costs or
frame/publication work. It reuses recorded wall costs, so it cannot establish
the performance of a future scheduler or batching implementation. Per-unit
predictions trained on the other two repetitions are an empirical check, not
a worst-case bound. The mathematical counterexample concerns a proposed exact
center cache; the current source-evaluated builder does not have that bug.

All 24 retained service commands require Vulkan validation. First establish a
matched validation-on/off Release performance control; the retained host costs
are not yet a production baseline. CPU deduplication and better packing alone
cannot supply the recorded Guardian cut work in four 4 ms windows.
Work granularity/submission organization and an explicit budget or
admission-rate choice belong in the next experiment. Long-session source/history
and field-authoritative alternatives remain separate decisions.
