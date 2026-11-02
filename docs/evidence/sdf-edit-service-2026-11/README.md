# Bounded edit-service evidence

This packet supports the [execution results](../../notes/sdf-edit-service-results.md)
and [frozen plan](../../notes/sdf-edit-service-execution-plan.md).
Measurements were taken on an NVIDIA GeForce RTX 5070 Ti in the existing Linux
Release build, using paced headless 1080p SS1 drawing with one completed reader.

- `summary.json` records the service factors, exact-cache checks, deadline
  outcomes, finite raw-pose integration and final regression results.
- `summarize.py` reproduces the summary from retained receipts without launching
  native jobs: `python3 -B docs/evidence/sdf-edit-service-2026-11/summarize.py`.
- `retention.json` inventories protected raw evidence, native executables,
  shader copies and absolute shader-directory dependencies. Nothing in an older
  study was deleted, replaced or regenerated.
- `source-pins.json` distinguishes the baseline, preparation, submission,
  integrated and stricter deadline-audit runtime identities.
- `verification.json` records source/test/documentation checks, deterministic
  summary replay, all protected-file hashes and 18 recomputed finite assessments.

Raw evidence: `outputs/sdf/edit-service-loop-20261101/`. Each cohort retains its
input, full native JSON, log, command, executable/shader/source fingerprints and
incremental cohort receipt. Native jobs were serialized. The runtime and the
actual completed draw determine latency; preparation is not moved off-clock.

Six source-pinned runtime executables are retained across the old baseline and
new controls. The superseded initial smoke executable is not retained; its exact
two source overlays are archived and hash-checked against the original receipt,
with every other project file matching `1e955297`. Primary qualification uses
the retained later executables. The complete new raw packet is about 299 MiB.

The static four-sphere control starts from retained parsed base/edited scenes,
as in the previous study. It measures copies, validation, building and drawing,
but is not a full live-input Guardian benchmark. The finite simple-scene route
does construct and parse new authored source on the worker after input receipt.
Startup capacity preflight/calibration is separate and is not reused as a
prepared future job in the measured route.

The first validation-on/off comparison uses the previous protected qualified
native. Subsequent source changes preserve its shader bytes. Validation-off
measurements are reported separately from validation-on correctness runs.
Removing validation layers is not an algorithmic speedup.

Prediction bounds are empirical maxima with a margin, not worst-case proofs.
The final runtime additionally records each unit's availability timestamp and
checks it against the absolute maintenance-window deadline. A unit that completes
late is a miss even if it is no longer active when polled. Earlier exploratory
receipts use the older active-at-poll rule and are retained as such.

The final regression packet retains regular/diagnostic geometry checks, four
byte-identical original-Cached replays of the finite retained scenes and four
early refusals. An initial render-only image command correctly produced no PNG;
the corrected readback captures are retained under `final-regression/replay-v2`.
This harness correction did not rerun or alter performance cohorts.

No public mode/default, shader, authored format, allocator, UI, undo product,
multi-reader route, XR route or representation fork was introduced. No push or
publication is part of this evidence.
