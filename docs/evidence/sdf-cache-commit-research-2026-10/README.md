# Cache-commit research evidence

Date: 2026-10-30. Intake: `e042beaf`, clean. Research and planning only; no
renderer/builder change or new native GPU timing.

- [Research and proposed experiment](../../notes/sdf-cache-commit-research-plan.md)
- [Independent CPU probe and receipt analysis](probe.py)
- [Results and source identities](probe.json)
- [Primary-reference ledger](references.json)
- [Scoped checks](checks.json)

Reproduce from the repository root:

```sh
rtk proxy python3 docs/evidence/sdf-cache-commit-research-2026-10/probe.py --output /tmp/sdf-cache-commit-probe.json
```

The probe checks fixed-knot half-quantized hard selection, workspace clipping,
interpolation, uniform-label ambiguity, primitive bounds and compact atlas
remapping. It reanalyzes the prior one-edit commit receipts and retains historical
regional setup costs. It records 273 current SDF/GPU-contract file identities and
the original projectR builder/compute hashes. It neither invokes native rendering
nor qualifies a proposed builder, performance gain or continuous editing.

The hard-selection model has no numeric differences but two signed-zero bit
differences in targeted ties. Native R16F encoding and shader arithmetic are an
explicit future gate. The previously measured preview/bake limitations remain.
