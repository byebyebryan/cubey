# SDF query acceleration research, 2026-10-29

[Proposed experiment plan](../../notes/sdf-query-acceleration-research-plan.md).
This pass inspects current code and primary research and computes capacity
scenarios. It does not implement or benchmark another rendering candidate.

- [Sources](sources.json): paper sections, pinned author code, access limits
  and the distinction between published methods and proposed Cubey extensions.
- [Capacity and frame-budget forecast](capacity-forecast.json): current metric
  identity, exact formulas, assumed child lengths and hypothetical Amdahl cases.
- [Previous completed results](../../notes/sdf-source-query-optimization-results.md):
  canonical implementation, qualification and current timing boundaries.

Baseline is `17a4219c`. Raw metric input remains at
`outputs/sdf/source-query-optimization-20261029/final-timing/guardian-side-combined-r1.metrics.csv`.
Its SHA-256 and the archived timing receipt hash are recorded in the forecast.
The compact forecast reads resource metrics from measured frame 30; it does
not treat the descriptive build/upload observation as a latency distribution.

Downloaded author source stays in ignored
`outputs/sdf/query-acceleration-research-20261029/references/`.
The adaptive source hash matches the earlier investigation's pinned file.
No external source is vendored by this pass. The 2023 publisher PDF was
search-indexed but direct retrieval returned 403; implementation details are
grounded in the pinned author code, not a claim to have read that entire PDF.

Proposed memory caps, parameter choices, pilot selection and edit-latency gates
are planning judgments. They are not adopted renderer contracts or measured
speedups. Sparse child references and construction cost remain unmeasured.

Validation: the three documentation hygiene/media/rules groups pass. JSON
parsing, capacity equations, scenario arithmetic, baseline receipt hashes and
downloaded source hashes were checked. No renderer tests or GPU jobs were
needed for this planning-only change.
