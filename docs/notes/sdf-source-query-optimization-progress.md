# SDF source-query optimization loop

Date: 2026-10-29. Contract: [execution plan](sdf-source-query-optimization-plan.md),
committed at `0737bec9`. Work remains local on `sdf-tracing-comparison`.

## Checkpoints

- Baseline frozen before implementation: binary plus all 116 existing SPIR-V
  files and 155 source/content fixtures in the selected protected directories.
  Raw receipts: `outputs/sdf/source-query-optimization-20261029/`.
- A candidate adds startup selection, ten opaque source-lit wrappers and
  requested/effective optimization receipts. General, current reference,
  sampled, optical and debug shaders remain available. Automatic still selects
  the reference pending qualification. No per-edit pipeline creation is added.
- A controlled GPU fixture calls the actual opaque `surfaceRadiance` function
  with forced trace exhaustion. It checks front/back/grazing/tiny-sign and
  nonfinite inputs, exhaustion propagation and geometric query origins.

Qualification, native window evidence, performance and the independent B trial
are pending. A candidate build is not adoption or interactive root-far support.
