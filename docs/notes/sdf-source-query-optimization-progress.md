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

- `09ca1d74`: A candidate checkpoint. Its 129-job main qualification passes,
  including ten frozen 155-check query routes, Guardian/Robot 1080p captures,
  analytic/hybrid history and 21-action per-frame eligibility/failure replays.
  Another 12 appearance jobs and 42 lighting/near-surface image jobs pass.
- The 14-case controlled GPU exhaustion/nonfinite/origin fixture passes. A real
  Xvfb source window survives two resizes/recreations, orbit, zoom, reset and
  material editing with no field build and exactly three pipelines each time.
- Rejected harness attempts are retained: missing resume metadata, then camera
  distances below the existing 0.6 admission limit. The completed edge suite
  uses an enclosing clipped sphere at admitted camera distances; no camera
  guard was weakened. The first documentation check needed an index link.

Performance and the independent B trial are pending. Automatic remains reference;
these checks do not establish interactive root-far support or physical acceptance.
