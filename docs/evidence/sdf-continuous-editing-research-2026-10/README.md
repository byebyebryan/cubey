# Continuous-editing research evidence

Research date: 2026-10-29 local / 2026-10-30 UTC. This is source review,
primary-reference research and independent mathematical/sequence diagnostics.
No production renderer, authoring UI, asynchronous builder or VR runtime is
implemented or measured here.

- [Research and proposed validation plan](../../notes/sdf-continuous-editing-research-plan.md)
- [Independent probe](probe.py)
- [Probe results and source identities](probe.json)
- [Primary-source reading ledger](references.json)
- [Scoped checks](checks.json)
- [Report assembly](build_report.py) and [static interaction checks](check_report.mjs)
- [Exported-file manifest](report-manifest.json)
- [HTML report: Putting continuous editing together](http://starship:8787/reports/0738ac47582410f1eb1387c0a5868b599675d18c170d7b06b78a59dfad1316eb/)
- [Publication receipt](../sdf-continuous-editing-research-report-2026-10.json)
  and [all-file HTTP verification](delivery-checks.json)

Reproduce from the repository root:

```sh
rtk proxy python3 docs/evidence/sdf-continuous-editing-research-2026-10/probe.py --output /tmp/sdf-continuous-editing-probe.json
```

The probe checks scalar compositions, one trilinear cell, sequence accounting
and scene limits. It neither ports a donor nor evaluates GPU tracing accuracy,
live-editor behavior or deadline compliance. Its source identities refer to
the inspected runtime and original projectR checkout, not a new runtime build.

The dedicated HTML export is at
`outputs/sdf/continuous-editing-research-20261029/report/`. The static lab
visualizes independent examples, not native Cubey rendering. Existing
2026-10-29 timings retain their own binary/workload identity in the linked plan.
