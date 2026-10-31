# Local update and pending edit research

[Research and proposed validation plan](../../notes/sdf-local-edit-research-plan.md).
Intake `79362e46`, clean `sdf-tracing-comparison`, 2026-10-30 PDT.
Runtime, shaders, defaults and source/history/material contracts are unchanged.
No new native GPU timing or local-updater implementation is included.

- [Locality analysis](analyze_locality.py) and [results](locality.json)
- [Pending composition and CPU admission probe](probe_pending.py) and [results](pending.json)
- [Primary references and transfer limits](references.json)
- [Source and resource identities](source-audit.json)
- [Scoped checks](checks.json)

`analyze_locality.py` reads the saved raw fields indexed by the previous
[qualified commit packet](../sdf-cached-commit-2026-10/README.md). It verifies raw
sample, cell and topology identities before analysis. The 55 unique canonical
fields contain 2,767,768,064 stored samples, with maximum absolute value
0.006763458251953125. The ideal active-brick real-function bound is
0.006767823467065927. The probe's additional `1e-4` padding is a diagnostic margin,
not a proved universal F32 guard or a production setting.

22 adjacent radius-.025 chain transitions have no observed sample, active-leaf
or existing-flag changes outside that diagnostic region. Raw brush AABBs miss
386 changed common-brick occurrences. Topology reachability also requires whole
exposed child blocks: 2,219 entry presence changes outside the pointwise mask are
explained by changed subdividing ancestors. The initial point-mask-only pilot
is retained at `outputs/sdf/local-edit-research-20261030/topology-mask-pilot.json`.
The final analysis explicitly distinguishes structural closure from scalar
coverage. It does not loosen a numerical gate to cover a failure.

The same analysis records candidate-leaf fractions at .02/.065/.15 placements.
These are geometric work estimates using a declared margin. They are not native
update timings, a speedup prediction or a qualification of an implemented mask.

`probe_pending.py` exhausts 174,762 hard-Boolean occupancy cases for zero through
eight tools and checks 64,000 random interval points. Native CPU-only admission
passes add/cut streams of twelve Guardian and sixteen Robot stamps, including
every prefix. That does not initialize Vulkan or qualify an eight-edit shader,
rendering performance, full stroke capacity or physical input latency.

Reproduce from the repository root after retaining/reconstructing the previous
native raw dataset. Use fresh output names; the pending probe requires a fresh
work directory. The existing isolated environment supplies NumPy.

```sh
rtk proxy outputs/sdf/quality-speed-loop-20261029/.venv/bin/python docs/evidence/sdf-local-edit-research-2026-10/analyze_locality.py --output /tmp/sdf-locality-new.json
rtk proxy python3 docs/evidence/sdf-local-edit-research-2026-10/probe_pending.py --work /tmp/sdf-local-pending-new --output /tmp/sdf-local-pending-new.json
```

Full/sliced transfer timings, numerical skip certificates, stable-slot local
updates, eight-edit interval rendering and scheduled publication remain proposed
native stages. The large-field R16 comparisons, current one-tool preview and
commit timings retain their original evidence and boundaries. No donor code is
copied. Commits stay local; no push or project-site deployment.
