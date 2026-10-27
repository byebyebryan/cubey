# SDF representation and hybrid design review evidence

Date: 2026-10-26

This package supports the
[design review](../../notes/sdf-representation-design-review.md). It broadens
the [field-update baseline](../sdf-field-updates-2026-10/README.md), whose native
measurements remain source-matched. No new native renderer or editor is
implemented here.

## Artifacts

- [Review ledger](review.json): original projectR revisions/messages and file
  hashes, Cubey source identities, numerical results and primary references.
- [Numerical probe](reconstruction_probe.py): independent float64 reconstruction,
  smooth-operation grouping, undo-information and snapshot-accounting checks.
  Dependencies are pinned in inline script metadata. This is research code,
  with no Cubey build target or runtime dependency.
- [Reconstruction figure](reconstruction.png), also available as
  [standalone SVG](reconstruction.svg).
- [Sub-grid sphere figure](subgrid-sphere.png), also available as
  [standalone SVG](subgrid-sphere.svg).

Figures show mathematical slices, not native Cubey renders. The fields plotted
are independent of z locally, so bilinear interpolation in their slice equals
trilinear interpolation there. Quantization converts normalized distances to
world units before R16 conversion. The actual sample spacing used for that
conversion is `1 / (256 * 7)` world units.

The radial diagnostic uses 4,096 directions, first sign-change brackets sampled
at `h/64`, and 50 bisections. It can miss narrower sign excursions and does not
provide a 3D Hausdorff distance. The zero sets isolate reconstruction behavior;
they do not reproduce native hit tolerances, GPU arithmetic, antialiasing,
materials or normal estimation.

## Reproduction

Copy the archived probe into a new empty directory under `outputs/sdf/` and run
it from the repository root with `uv run --no-project` and that copied script
path. It writes JSON, PNG and SVG beside itself. Use a fresh directory to keep
these archived results intact. The recorded Python/package versions and probe
hash are in the ledger.

The source review can be checked using `git show` at the recorded revisions in
the local `/home/bryan/code/projectR` repository. Raw source-review receipts,
stdout and the original probe outputs remain under
`outputs/sdf/design-review-20261026/`. No Unity reproduction, GPU candidate
benchmark or new native visual acceptance is claimed by these artifacts.
