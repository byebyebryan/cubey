# Native TAA foundation evidence

This packet qualifies the opt-in shared TAA implementation against baseline
`13997bc1911408df2775748a8e05b854fd617852` (plan only, based on main
`211903e865fab09edd4f740509765f4eda016fa2`). The [results note](../../notes/taa-foundation-closeout.md)
states the accepted scope and quality/cost limits. The [architecture](../../architecture/temporal-aa.md)
owns current API semantics.

The local visual artifact is
`outputs/taa/foundation-loop/review/index.html` in `/home/bryan/code/cubey-aa`.
It contains matched full views/crops, convergence/cut controls, intact/section
comparisons and paired native PBR movies. It is local review delivery; no
public-site deployment is claimed. Agent pixel/playback review is recorded
separately from physical desktop behavior and user aesthetic acceptance.

## Retained records

- `identity.json`: 841 native source files, two Release executables and their
  250 SPIR-V artifacts. All 1093 entries were rechecked after capture.
- `baseline-identity.json` and `baseline-captures.json`: pre-integration
  executable/shader identity and native baseline commands.
- `capture-calls.json`: final native SDF/PBR capture and serial-cost commands,
  actual exit codes and validation scope.
- `capture-configs.json`, `motion-actions.json` and `section-actions.json`:
  full added comparison presets and deterministic replay inputs. Repeated scenes
  are stored once in each action packet. Commands retain original ignored paths;
  restore those inputs or override their paths when reproducing.
- `results.json`: matched-view quality, serial GPU costs, memory, history ages,
  AA-off equality, section identities, bypasses and native window checks.
- `reference-identity.json` and `reference-parity.json`: pinned MIT donor,
  external compiler/adapter identity and the 24-frame numerical comparison.
- `gates.json` and repository logs: exact required build/test results.
- `acceptance-audit.json`: final plan-gate disposition, including the declared
  P0 timing deviation and the limits of visual acceptance.
- `artifact-manifest.json`: hashes and retained locations of original captures,
  metrics, videos, logs and the prepared local report.

Large native images, videos, raw HDR fixtures and all exploratory/failed attempts
remain ignored under `outputs/taa/foundation-loop/`. `final/` is the integrated
capture set; its complete 384 observatory frames are byte-identical to the
earlier visual set. Superseded evidence is retained as history and is not
silently counted as a fresh pass.

Expand an action packet before passing it to `--comparison-actions`:

```bash
python3 tools/taa/materialize_actions.py \
  docs/evidence/taa-foundation-2026-11/motion-actions.json \
  outputs/taa/replay-motion-actions.json
```

## Evidence boundaries

The observatory's 128 matched submitted views include ages 0,1,2,4,8,16,32,64,
orbit/stop/return, then a cut at frame 113. Section and matched intact stills use
the approved cutaway camera with 65 TAA updates. Geometry/appearance identities
are compared within each matched control. AA-off SDF and workshop decoded
pixels equal their original baselines. Requested TAA on raw diagnostics, SS2
and transmission preserves the unjittered Off pixels and records bypass reasons.

Rigid workshop movies contain 211 submitted poses at 30 frames/s; cloth movies
contain 128. Their deterministic capture clock is not a measurement of physical
desktop refresh. Actual virtual-window tests separately exercise Off/TAA,
reset and resize with clean validation. The first PBR virtual-window attempt
inherited Wayland and was closed when the X11 driver could not find it; only
explicitly forced X11 runs are acceptance evidence. An initial unused vertex
attribute warning was fixed and the final native captures are clean.

GPU costs use serial render-only runs with validation, encoding and pixel
readback disabled. SDF excludes the first 24 of 96 frames; forward excludes
64 of 512. Memory figures distinguish requested payload from dedicated Vulkan
allocation requirements, excluding descriptors/pipelines and whole-process
residency. Full dev and synchronization gates are distinct from those costs.

The numerical oracle specializes public MIT Donut source to the same native
subset as Cubey. It records binding/uniform, finite/viewport and dispatch-edge
adaptations. DXC is qualification-only. See [license and port boundaries](../../../third_party/temporal_aa/README.md).

The full dev run completed all 301 unique tests with `Passed` records and the
final 100% summary. Its outer command wrapper was terminated before collecting
the CTest exit status, so `gates.json` records that exit as unknown and retains
the wrapper's 143 status. The suite result is verified from its complete log;
no zero exit is fabricated. Final focused and synchronization lanes have their
own collected exit codes. The later test-only rigid-motion probe is covered by
those final lanes without changing the captured production code.
