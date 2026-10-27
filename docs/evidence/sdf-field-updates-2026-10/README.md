# SDF field-update investigation evidence

Date: 2026-10-26

This package supports the
[field-update investigation](../../notes/sdf-field-update-investigation.md).
It records measurements of the unchanged renderer at source
`d0ae6a74b96a4c0e02a1d9dc0600de0b6f7b278a`; it is not evidence of an accelerated
or incremental builder.

## Retained artifacts

- [Measurements and provenance](measurements.json): three cold runs for each
  of four scenes, binary and all 29 compiled shader hashes, commands, all 65
  forward-history update timings, allocation arithmetic, CPU probe summaries,
  locality counterexamples and inspected donor revisions/file hashes.
- [Guardian CPU probe samples](cpu-guardian.json) and
  [Robot CPU probe samples](cpu-robot.json): raw measured distributions,
  shape/operator counts and scene hashes.
- [CPU measurement harness](cpu_probe.cpp): an archived standalone research
  harness linked against the existing Release scene/history objects. It
  implements no acceleration and adds no project build target.

Raw native logs, the profiler CSV/trace files, build/link command receipt,
baseline runner, analysis scripts and fetched reference files remain in the
ignored local directory `outputs/sdf/field-update-investigation-20261026/`.
Their hashes are included in the measurement ledger. No downloaded donor code
or scene asset is promoted into Cubey by this package.

## Reproduction scope

Build the Release SDF application and its four existing C++ test targets with
the `release` build preset. The `cold_runs` entries retain exact argument
vectors. They use required Vulkan validation, atlas axis 64, depth 8, fixed
8-cubed R16F bricks, two headless render-only frames at 320x180 and supersample
1. Run cases sequentially; repeat each three times in the recorded shuffled
order. Field resolution is independent of output extent.

The `history_run` entry retains the 196-frame construction invocation and its
profile-output prefix. The saved log must include exactly history states 1–65,
in order, in addition to the initial empty field. Extract `sdf.prepare_geometry`
from the generated passes CSV; the ledger identifies the 65 changed-state
spans. This measures CPU preparation through publication, excluding event
dispatch and first visible presentation.

The CPU harness takes one scene path. Build it with the same flags as
`cubey_project_sdf_authored_tests`, replacing only that test's main object and
linking its other existing objects/libraries. Run on the Guardian marble and
Robot field-kit scene files. It uses seven warmups, 31 samples for each history
prefix, and 101 samples for each hash operation. Report warm-filesystem loading
separately from prefix preparation; the full-history shortcut is intentionally
measured separately.

These artifacts are source-pinned observations. A future run needs a new output
directory and ledger, rather than overwriting this evidence. Native field
oracles, backward/mutation timings, a running donor implementation and a new
builder's performance/visual acceptance remain separate work.
