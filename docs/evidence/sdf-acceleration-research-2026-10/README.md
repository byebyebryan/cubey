# SDF acceleration research evidence

Date: 2026-10-28. See the
[research and experiment plan](../../notes/sdf-acceleration-research-plan.md).

This is a primary-source search and planning pass over the measured Guardian
problem. It adds no native renderer change, imported backend, new GPU benchmark,
performance result, or adopted default.

- `sources.json` records ten primary references, inspected scope, Cubey-specific
  assessments, and availability/qualification limits. Publication dates are
  separated from crawl dates; Generalized Lipschitz Tracing appeared online in
  2024 and in a 2025 issue.
- `context.json` pins the local baseline and inspected runtime/evidence files.
  Their hashes remain unchanged by this pass. The earlier native measurements
  stay in the Guardian evidence pack.
- `contracts_probe.py` and `contracts-probe.json` check independent mathematical
  examples used to design future tests: a valid step crossing a cell, invalid
  parent-center reuse, field-value versus world-space padding, a smooth parent
  changed by child rescaling, and the importance of the final workspace clip.

Run the planning probe from the repository root:

```sh
python3 docs/evidence/sdf-acceleration-research-2026-10/contracts_probe.py
```

Its output is mathematical evidence for the stated counterexamples. It is not
native renderer qualification, a full proof of a future bounding algorithm, or
permission to relax the frozen surface tolerances.

The far-bound method is grounded in the already-pinned Lipschitz Pruning donor,
but a sparse hierarchy or partial update scheme would require new Cubey work.
No paper/demo speedup is presented as a forecast for Guardian. Research code
availability and a previous isolated donor test remain distinct from native
integration and performance acceptance.
