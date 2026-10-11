# Corrected offscreen and native-Wayland performance study V2

V1's worker throughput/stability results remain useful, but its windowed frame
measurements include Xvfb presentation and are not native desktop FPS verdicts.
Retain V1 unchanged; this study supplies the intended rendering-path controls.

Reuse the shared offscreen host, existing native renderer and immutable V1 cases.
No solver/math/native-extension edits, new terrain, CUDA interop, material changes
or automatic default promotion. Use Release, mountain 1k/16m and 2k/8m on the
same 16.384km footprint; rolling 1k/16m confirms pooling. Keep 512mm/h forcing,
source binding, cameras and accepted Scenic/B-spline 2x settings fixed.

The opt-in shared `--headless --headless-render-only` path needs `--frames N`
or `--run-seconds S`. It uses two in-flight slots and performs no image readback,
encoding or presentation. `--render-fps 60` is operational wall-time pacing;
zero is unpaced. Existing PNG/video capture timing and output remain unchanged.
`--run-seconds` and `--render-fps` also bound/pace a native window without input
automation. Selected GLFW backend is logged, not inferred from environment.

Live offscreen uses the same external-session update, validation, health, upload
and rendering logic as the GUI. Physical time belongs to the solver; render
updates use wall time. Recorded render-only controls hold exact saved fields.
Passive viewers follow an external controller's reset only with its matching
validated command/acknowledgement. Explicit accepted reinitialization is marked
delayed with a thirty-second bound, rather than claiming healthy/fresh fields;
ordinary publication disconnect still uses the existing three-second threshold.
The validation controller retains the latest reset command until the passive
viewer observes the applied generation, before publishing another command.
This is not a multi-controller or arbitrary missed-reset-history protocol.

Before primary timings, validate contracts, existing capture behavior, actual
advancing offscreen publications, pause/resume/reset and clean shutdown. Verify
headless operation with DISPLAY/WAYLAND_DISPLAY unset, and native Wayland via
actual backend logging with DISPLAY unset. No Xvfb/XWayland/software-device
fallback. Archive actual viewport, GPU, build/input/native hashes and commands.

Primary paired workloads: worker-only; held renderer-only; identical held
renderer plus a computing/publishing worker; and live renderer plus worker.
Run three repetitions for both mountain configurations on offscreen and native
Wayland where available, with explicit warmup and bounded timing windows.
Report worker progress windows rather than pretending identical solver loads.
Separate unpaced throughput, deliberate pacing, CPU wall spans/waits, GPU work,
publication freshness, upload and whitewater costs. Never subtract independent
percentiles as an attribution technique. UI costs are windowed-only.

Afterward: operational 60Hz checks, selected 1440p/4K held controls, rolling
confirmation, and a ten-wall-minute offscreen live soak. Capture/readback/encode
costs are outside primary render-only windows. At most two small optimizations
are allowed only after identifying a repeatable bottleneck; keep originals and
rerun affected controls. Performance-target failure does not become acceptance
because regression tests pass. Human visual/monitor acceptance stays separate.

The first frozen baseline exposed one host-policy mismatch: continuous
offscreen inherited synchronous whitewater classification from PNG/video
captures, whereas native GUI already uses a bounded asynchronous job. The
targeted correction reuses that existing GUI policy for render-only execution;
deterministic captures stay synchronous. Retain all 42 original primary trials
and their app hash. The separate `--stage corrected` supplies three affected
live-offscreen repeats per grid plus one held-offscreen and one live-Wayland
control per grid (10 supplemental trials). `--stage secondary` supplies the
14 paced/resolution/rolling/soak trials with the corrected app. These are not
pooled with the old app's timings. Include sampled maximum service time as
well as p95, solver pace, and actual whitewater source age: removing blocking
work does not prove either lower GPU contention or fresher decorative cues.

Publish one HTML report under outputs with original and corrected host identities,
measurement receipts, failures, matched visuals and practical recommendations.
Commit validated checkpoints; no automatic push.
