# Hardware ray tracing first-pass evidence

Date: 2026-11-04. See the [results](../../notes/raytracing-first-pass-results.md)
and [native application guide](../../../projects/raytracing/README.md).
Runtime source: `92bc389d`; local branch `raytracing-first-pass-20261104`.
Large source data, binaries, logs and captures remain in the isolated worktree's
ignored `outputs/raytracing` and `cache/raytracing` directories.

| Evidence | Contract |
| --- | --- |
| [Runtime identity](identity.json) | Source, empty source patch, binary/model/SPIR-V hashes, GPU/driver |
| [Reference run](reference-run.json), [parity](reference-parity.json) | Unmodified pinned Khronos native reproduction and barycentric parity |
| [Dragon manifest](../../../projects/raytracing/assets/stanford-dragon.json) | Exact archive/member/prepared hashes, actual counts, transform and normal policy |
| [Geometry](geometry.json) | Four independent double-precision camera cases, tolerances, scored rays/errors |
| [Window](window.json), [image parity](window-parity.json) | Owned Xvfb controls/resize, unchanged AS builds and saved-view reproduction |
| [Legacy runs](legacy-runs.json), [parity](legacy-parity.json) | Representative SDF/PBR captures, validation and decoded RGB hashes |
| [Validation scope](verification.json) | Dev suite plus focused reruns, source scopes, unexecuted acceptance boundaries |
| [Qualification commands](qualification-checks.json), [final commands](qualified-checks.json) | Numerical/window/test runs and final RT tests/timing packet |
| [Timing summary](timing-summary.json), [all timing runs](timing.json) | Three repetitions per view/shadow route, timestamp ranges and GPU boundary observations |
| [Captures](captures.json), [startup/memory](startup-memory.json) | Nine validated images, launch-to-PNG time and separately scoped preparation/resource costs |
| [Retention](retention.json) | Kept local paths, excluded failed investigations, original worktree preservation |
| [Report delivery](delivery.json) | Immutable report URL, HTTP/asset integrity and open browser acceptance |

Run the packet again from the repository root after building the optimized
application and preparing the model:

```sh
rtk proxy python3 -B projects/raytracing/tools/profile_first_pass.py --binary build/release/projects/raytracing/raytracing --output outputs/raytracing/review-packet
```

The output directory must not exist. Timing and captures run serially. The
script records current source/binary/shader identities, so a later run belongs
to that later source. Keep it separate from this dated packet.

This evidence does not establish physical display latency, human artistic
acceptance, actual unsupported-GPU rejection or process-wide peak allocation.
No source push, merge or public demo-site publication occurred.
