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

During execution the user explicitly requested **headless only from here**.
The completed 42-slot baseline retains its native Wayland controls, but the
two extra corrected-native confirmations, two paced-native checks and native
screenshots are deferred. Continue both remaining stages with `--hosts offscreen`
and capture with `--headless-only`. The amended loop has 62 completed timing
slots: 42 frozen baseline, 8 correction controls, and 12 operational/resolution/
rolling/soak checks. The reporting root retains `study-scope.json` with this
authorization; do not silently describe the deferred checks as measured.

## Completed result — 2026-10-10

Retained study: `outputs/fluid/offscreen-perf-20261010-CwWtK3/`.
The [HTML review](http://starship:8801/fluid/offscreen-perf-20261010-CwWtK3/)
contains raw receipts, host identities, timing ranges, exact-state captures and
the separate performance-budget misses. All 62 amended timing slots completed
and the final raw-evidence audit reports no failures. One user-closed native
window remains an incomplete attempt; its replacement used the frozen original
executable and is identified through `phase-aliases.json`, not an overwritten
receipt. The four additional native controls and native screenshots remain
deferred, as requested.

| Live offscreen, 1080p, render cap 60 Hz | Physical / wall | CPU service p95 | Field age p95 |
| --- | ---: | ---: | ---: |
| Mountain 1k / 16 m, 45 s | 59.97× | 2.80 ms | 0.140 s |
| Mountain 2k / 8 m, 45 s | 3.88× | 15.16 ms | 0.315 s |
| Rolling 1k / 16 m, three 45 s runs, median | 60.05× | 2.73 ms | 0.146 s |
| Mountain 1k / 16 m, ten-minute soak | 59.99× | 2.70 ms | 0.143 s |

The soak's GPU scene p95 was 5.29 ms, sampled CPU service maximum 5.21 ms,
and loop p95 16.74 ms including intentional pacing. Worker RSS median stayed
608.37 MiB; viewer RSS early/late medians were 427.93/430.20 MiB (+2.26 MiB).
Both owned processes exited zero without escalation. This supports bounded
stability, not indefinite leak freedom or guaranteed monitor FPS.

The one measured correction is worthwhile: 2k unpaced service p95 fell from
125.59 to 23.90 ms by sharing the existing asynchronous whitewater policy.
It also exposed the contention tradeoff: unpaced 1k rendering left the worker
at 21.61×, while pacing restored ~60×. At 2k, worker-only throughput was already
8.88–9.82× including publication; the paced live trial still misses the ≥54×
fast-simulation target. Keep 2k as a finer-grid/slower-live or offline option,
not the fast-live recommendation. No second optimization was attempted; the
2k budget misses remain explicit.

Held 1k renderer-only service p95 was 3.15/3.26/4.36 ms at
1080p/1440p/4K (one corrected 1080p control, three larger-output repeats).
These isolate output resolution, not finer simulation or live 4K capacity.
Prefer 1k/16 m with a 60 Hz render cap for this fast-live workload, without
changing launcher defaults. If faster 2k becomes important, separately profile
solver/publication and scene geometry before choosing further work.

Validation retained under the study root: 20 Release CTests, two Debug CTests,
the post-fix actual offscreen pause/resume/reset check, and the final helper
CTest (12 Python contract tests). Four actual Vulkan PNGs were inspected;
both held 1k/2k images are byte-identical to the archived pre-fix executable.
These are static endpoints, not proof of motion or human visual acceptance.

`audit_offscreen_perf_v2.py --study <V1-root> --out <V2-root>` recomputes each
timing summary from raw CSV and rechecks the exact slot set, continuously
advancing worker samples, frozen source/native/shader identities, actual
backend/device/viewport, owned cleanup, reset control and capture byte proofs.
Run it with the same optional SynxFlow environment used by the campaign.
`capture_offscreen_perf_v2.py` collects separate visual receipts; pass
`--headless-only` and `--baseline-app <archived-app>` for this amended study.
The audit never starts applications; capture with this flag never opens native
windows. All exact commands and per-trial binary identities are retained in
protocol/receipt JSON.

### Subsequent approved launcher promotion

After reviewing these results, the user approved live + 1k/16 m + 60 Hz as the
prepared terrain-demo launch default. `run_terrain_rain_demo.py` now selects
that preset and the measured Release build when mode/app/pacing are omitted.
Explicit replay, other configurations, custom caps and `--render-fps 0` remain
available. Startup stays paused until Space resumes the worker. This follow-up
changes only the prepared launcher; the frozen study receipts, shared host,
other launchers and Cubey's builtin solver defaults are unchanged.
