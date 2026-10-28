# SDF acceleration first-loop evidence

Date: 2026-10-28. See the [decision report](../../notes/sdf-acceleration-loop-results.md).

- [summary.json](summary.json): grouped frame/action ranges, query work and images.
- [timing.json](timing.json): 318 accepted ordinary native timing runs, commands,
  resources and shared-GPU load; skipped resolution groups remain unmeasured.
- [workload.json](workload.json): separate primary/normal/shadow query counters,
  three-view differential checks and local binary payload paths.
- [actions.json](actions.json): 180 accepted existing-content history transitions.
- [qualification.json](qualification.json): six sets of frozen geometry checks,
  semantic undo/publication replays and optical captures. Individual semantic
  action receipts are retained alongside it.
- [context.json](context.json): source/binary/shader hashes, preservation and raw
  receipt identity. Counter records are 256 bytes: original 52 uints followed by
  three `uvec4` aid records (lookup, far advance, far exact fallback, root reject).
  Ray-result records are 32 bytes. No timing is inferred from these counters.
- [images.json](images.json), `images/` and `optics/`: native PNGs and comparisons.
- [material-boundary.json](material-boundary.json), `boundary/`: focused native
  normal/owner/base-color diagnostics for the isolated high-contrast Robot pixel.
- [memory-envelope.json](memory-envelope.json): conservative requested-payload
  bookkeeping, including old/new regions and staging/CPU allowances. Not process RSS.
- [cpu-fixture-correction.txt](cpu-fixture-correction.txt): rejected incomplete
  synthetic test fixture and its correction; no renderer change was involved.
- [checks.json](checks.json) and [final-checks.log](final-checks.log): seven CPU
  and three documentation checks pass. GPU/native checks remain separate.
- [report-delivery.json](report-delivery.json): immutable HTML URL, HTTP hashes
  for all 59 bundle files, eighteen filter combinations and desktop/mobile QA.

Raw profiler CSVs, ray/counter payloads, logs and superseded action-control
receipts remain local under `outputs/sdf/acceleration-loop-20261028/`.
The existing NVENC encode ran throughout the measurements; no idle-device
acceptance, physical display latency, default adoption, push or deployment is claimed.

## Reproduction

Build the Release `cubey_project_sdf` target, then run these tools from the root.
Every destination must be fresh. Do not run other owned CPU/GPU jobs during a
measurement. Use an idle machine for a later dedicated acceptance pass.

```sh
python3 projects/sdf/tools/acceleration_loop.py qualify --output QUALIFICATION
python3 projects/sdf/tools/acceleration_loop.py workload --output WORKLOAD
python3 projects/sdf/tools/acceleration_loop.py timing --qualification QUALIFICATION --output TIMING
python3 projects/sdf/tools/benchmark_acceleration_actions.py --output ACTIONS
```

The assembly tool expects the retained raw subdirectory layout. It copies
completed receipts and verifies protected content/historical shader hashes;
it does not fabricate missing timings or regenerate native captures.
