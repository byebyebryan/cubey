# Fluid 2.5D

`fluid_25d` is Cubey's terrain-water project: a 2D shallow-water simulation
over a heightfield terrain. Water should flow downhill, pool in basins, follow
an authored channel, and drain through an explicit sink without requiring a
full 3D fluid volume.

The broader technique map lives in
[`docs/architecture/fluid-simulation.md`](../../../docs/architecture/fluid-simulation.md).

## Status

The October 4 integration adds an optional, interactive external SynxFlow
service alongside the unchanged built-in default and recorded fallback. Cubey
shares a project-local scene/session contract across these paths; CUDA remains
in a separately launched GPL worker, not in the Vulkan application. See the
backend/service sections below for controls, acceptance evidence and limits.

River V0 has a project-local GPU checkpoint. `fluid_25d` is a windowed and
headless Vulkan application with a GPU-resident virtual-pipes default and an
opt-in GPU finite-volume comparison solve, plus a
deterministic oblique terrain-and-water catchment presentation. Top-down
diagnostics remain explicitly selectable. The default Composite catchment
view preserves the original terrain-and-water shading; Water Isolation
quiets the bed to expose the wet edge, Flow Inspection adds a fixed-grid
velocity-arrow field, and Transport Inspection adds an opt-in conservative dye
pulse to eligible finite-volume source/outlet and terrain studies. It has deterministic
dry-bed, lake-at-rest, and river source/sink fixtures; headless oracle lanes compare
virtual-pipes depth/face-flux/velocity/ledger and finite-volume
depth/momentum/velocity/ledger against their corresponding CPU oracle.

Terrain-Water Audition V1 adds an opt-in, terrain-case-only discovery boundary:
an immutable `cubey.terrain.heightfield.v1` crop receives either a uniform rain
pulse or a uniform sheet release, and every outward perimeter face drains. The
same parameters must be used for every crop. These protocols are not a
hydrology generator, a water table, or product selection; they are a way to
observe solver response before humans choose a case.

The Config V2 product default is a bounded `256x128` catchment. Focused
CPU/GPU oracle fixtures intentionally set smaller dimensions so their evidence
stays quick and inspectable.

The project-local GPU timing lane records one aggregate `fluid_25d solver`
span per profiled simulation frame and reuses the common `--profile-output` and
`--profile-warmup-frames` options. The dated Release artifact and evidence
boundary are recorded in
[`docs/notes/fluid-25d-river-v0-checkpoint.md`](../../../docs/notes/fluid-25d-river-v0-checkpoint.md).

The CPU target remains a reference for the GPU implementation, not a shared
fluid framework and not a claim of scientific Saint-Venant fidelity.

Terrain-Water Audition V1 met the numerical-replacement trigger: untouched
30 m relief made the virtual-pipes positivity limiter look like grid-scale,
multi-cell transport rather than a credible wetting front. An isolated,
opt-in finite-volume Saint-Venant CPU/GPU comparison now exists to evaluate
that decision. It has no product claim: `virtual-pipes` remains the default,
and neither solver has selected or tuned a terrain case.

## Product boundary

### Backend selection

`--fluid25d-backend builtin|external|recording` makes the producer explicit.
Omitting it preserves the existing default and legacy path flags. `builtin`
uses the existing GPU solver (`--fluid25d-solver virtual-pipes|finite-volume`);
`external` requires `--fluid25d-external-session` (interactive service) or
`--fluid25d-stream` (retained viewing-only bridge); `recording` requires
`--fluid25d-recording`. Conflicting selections fail before startup: there is
no silent fallback or numerical-state transfer between solvers. The stock
external stream remains viewing-only; it does not support solver pause or
rainfall edits. Built-in controls operate on the GPU simulation. Neither
built-in method is being promoted as equivalent to the tested mountain solver.

### Project-local backend contract

`fluid_25d_backend_contract.h` defines the project-local, versioned scene/grid
identity, field availability, session lifecycle, frame header and command/ack
envelopes. Solver controls and viewer playback controls are separate capabilities;
unsupported fields or controls are never inferred from the backend name. The
guard rejects stale generations, regressing clocks/frames and duplicate acks,
with at most one outstanding command. This is not a universal fluid framework,
and backends are selected at startup rather than hot-swapped.

Each application now emits `fluid_25d_backend_startup_metadata:` once and shows
the adapter/input fingerprints under **Backend details**. Built-in descriptors
hash the actual initial bed/depth/source/sink/boundary arrays and numerical
settings; recording/stream descriptors use immutable input provenance and
exclude the growing output prefix or producer status. Solver-bed hashes cover
the exact row-major little-endian float32 bytes. A live viewer owns a separate
logical playback session, not the producer's lifecycle. The in-process adapters
now publish runtime frame headers and boundary acknowledgements through the same
guard. Built-in completion uses a separate 16-byte GPU observer at 10 Hz and on
controls/final capture, never full hydraulic-field readback. It counts accepted
substeps; rejected FV candidates cannot advance the reported clock. The observer
does not write solver fields or alter any existing numerical shader. Pause,
resume, reset, stop and pacing affect the built-in producer. Generic numeric
rain editing and single-step commands remain unavailable there; existing narrow
scene controls remain distinct.

Recording/stock-bridge headers describe the logical viewer playhead, not native
computation. Saved field time is separately displayed. Navigation loads the
selected state before announcing a new generation; late asynchronous loads
cannot replace another requested frame or viewer generation. Playback pause/rate/seek/restart never
change the recorded or external producer's hydraulics.

### Bounded external-service transport and viewer

The separate `fluid_25d_external_session` client defines a bounded local binary
transport for the new service: immutable contract metadata and solver bed,
three replaceable depth/qx/qz slots, and an atomic state publication carrying
the current lifecycle plus any command acknowledgement. The reader checks
identity, generation, precision, size and SHA-256 before accepting fields.
An overtaken display slot may be retried; persistent corruption fails closed.
POSIX file locking permits one controller, with one outstanding command.
Stop's final fields and its acknowledgement are accepted atomically; ordinary
frames cannot follow a terminal state. On Linux, attach the draw-only Cubey viewer
to an explicitly launched worker:

```sh
# These are explicit, separate foreground processes; use a fresh session leaf.
python projects/fluid/fluid_25d/external_synxflow/session_worker.py \
  --extension /absolute/path/to/audited/flood.so \
  --case /absolute/path/to/retained-case --out ./outputs/fluid/new-session --paused

build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-backend external --fluid25d-external-session ./outputs/fluid/new-session
```

The Python process needs the retained native/NumPy environment. Cubey does not
build or launch it implicitly. The viewer exposes acknowledged solver
pause/resume/reset/step/stop, uniform rain override in mm/h, and host pacing;
Space/R address the solver, not playback. Closing the viewer only detaches.
Typed edits survive heartbeats. A publication delay is explicit; after three
seconds without a changed valid publication the viewer freezes cues, disables
controls and returns nonzero on exit. Failed service state also returns nonzero;
normal stopped/completed states remain viewable without polling a dead producer.

### Optional source-build audit

`build_synxflow_source_v1.py` builds an explicitly pinned SynxFlow 1.0.1 archive in
a fresh private directory using an explicitly supplied CUDA toolkit and CPython
3.11. It does not install anything or add CUDA to Cubey. The default `legacy11`
lane leaves every upstream source file unchanged; `blackwell13` changes only
the two build-system files for current CUDA compatibility. The archive checksum,
commands, extension checksum and logs are retained in the output directory.
The default `--source-pin pypi-1.0.1` retains the original sdist diagnostic;
`--source-pin github-1.0.1` selects upstream commit
`8a9781504204a7b41217b483db4190c1f6f340bc`. These equal version labels do not
identify equal numerical source. Both pins require their exact archive checksum
before toolkit inspection or output creation.

`check_synxflow_source_correspondence_v1.py` runs that extension on fresh,
byte-identical copies of retained case inputs, using a predeclared protocol.
It compares every exported depth/momentum timestamp and the original analytic
controls, stops after a failed case, and never edits the reference inputs or
loosens the comparison gates. Its unit tests require the study's NumPy environment
and do not run the solver.

The initial October 4 PyPI-source audit did **not** pass the promotion gate. Both builds
matched all nine small controls exactly. On the mountain case, aggregate depth
and momentum differences stayed within the frozen tolerances, but localized
momentum differences did not. A fresh run of the released wheel matched all
723 reference field files exactly. A source audit then found semantic differences
in 11 of 125 numerical files between that sdist and the upstream release tag.
The unmodified tag, built with the same retained CUDA 11.7/GCC 11.4 toolchain,
passed all nine controls and both 14,400-second mountain rain/recession cases
under the unchanged frozen gates. See `source-tag-verdict.json` alongside the
retained failed `source-build-verdict.json` under
`outputs/fluid/backend-integration-v2-20261004-wlELKz/`.
This clears the source-build prerequisite, not the later interactive-service
acceptance gates. The released wheel and stock live viewer remain available.

### Optional GPL external service worker

`external_synxflow/` is a separately licensed GPL-3.0-only worker/build slice.
Its explicit build helper pins the correspondence-passing upstream tag and adds
host-boundary hooks to the native application, leaving all solver-library files
and the stock `run()` function unchanged. Nothing builds, installs or runs CUDA
from Cubey's normal configure/build/test workflow. Private build trees are
ignored; use a fresh leaf under `outputs/fluid/` for new builds and experiments.

The modified stock API and no-op service hook both passed all eleven retained
cases: all 3,690 exported field comparisons were exact; service timestep logs
were byte-identical. The direct binary snapshots retained native float32
precision and passed the predeclared six-decimal ASC quantization checks.
Source and parity evidence is in `service-parity-verdict.json` under the same
October 4 evidence directory. Native output clipping's scheduled zero-duration
bookkeeping pass is preserved, not removed or treated as an unexpected stall.

`session_worker.py --extension PATH --case RETAINED_CASE --out FRESH_DIR` is an
explicit foreground process, separate from Cubey. Its default is continuous
stateful operation at 60 physical seconds per wall second; `--finite` is an
explicit diagnostic option. Host pacing never scales the numerical timestep.
The worker publishes the three-slot binary transport, holds commands at checked
CUDA boundaries, and rebuilds the complete native state for reset. Rain changes
are uniform full-map rates (0..0.01 m/s); pacing supports 0.125..300x. Step waits
for one positive physical advance, preserving any intervening scheduled zero
pass. Immutable inputs are not rewritten. The latest 64 control records are
retained, not an indefinitely growing command log. Worker float-clock stalls
remain explicit failures; continuous does not promise unlimited time precision.

`run_external_service_controls_v1.py` explicitly exercises the native worker
through the same MIT C++ client used by Cubey. The October 4 study passed:
paused planes held byte-exact, rain edits preserved water, reset restored the
initial planes byte-exact in a new generation, and twenty warm acknowledgements
had p95 106.7 ms. The first attempt's wire-command spelling failure remains
retained beside the successful retry; canonical command spellings now have a
dedicated unit test. This is real control evidence, not render freshness,
long-running storage or human GUI acceptance. See
`native-controls-service-2/result.json` in the October 4 evidence directory.
The stock V1 bridge and recordings remain the retained fallback.

### Continuous-service acceptance and limits

`run_external_service_lifetime_v1.py` is an explicitly invoked native/GUI study,
not a default test. It requires the audited extension, native case/environment,
MIT probe, viewer, fresh output leaf and a protocol frozen before candidate
execution. The retained October 4 retry passed 600 wall seconds of continuous
stateful simulation and rendering on the unchanged 512x512, 30 m mountain input.
Native time reached 36,007 seconds before the final control sequence. Rain-off/on
edits and twenty warm pause/resume acknowledgements passed; acknowledgement p95
was 101.6 ms. The worker stopped/reaped normally, with three fixed slots, at most
64 control records, zero native output files and unchanged reference inputs.
Worker RSS varied by only 0.024 MiB after the first 60 seconds.

At the compositor's actual 1280x1432 extent (larger than the requested 960x640),
concurrent rendering p95 was 0.914 ms against a frozen 16.7 ms budget. The matched
paused/idle-GPU observation was 0.619 ms: a 47.6% relative slowdown remains an
accepted shared-GPU limitation, not an interop-free performance claim.
Publication-to-CPU-side-render-command freshness p95 was 0.184 seconds (maximum
0.973 seconds), against a 1-second p95 gate. This is not physical scanout timing.
The first completed soak was not accepted because its harness interrupted a
large profile export at shutdown; the retry allowed 120 seconds for export
without changing the simulation or measurement gates. Full uncapped profiling
is an explicit diagnostic that grows viewer memory/files; it is not the normal
viewer/worker storage policy.

`run_external_service_failure_v1.py` separately passed real GUI Space/R
resume/pause/full-reset controls, detach/reattach without producer termination,
3-second heartbeat-loss fail-closed behavior, and native callback-error unwind
and reaping. The harness requires an explicit keyboard driver using physical
evdev scancodes: stock `wtype`'s sequential virtual map did not reach GLFW's
physical-key shortcuts. The retained private wtype study build changed only
that diagnostic driver, not Cubey, the installed tool or the solver.

`run_local_backend_lifecycle_v1.py` additionally passed real GUI pause/resume/
reset for both built-in methods on tiny dry fixtures, and Completed-to-Reset
navigation on a synthetic recording with its fixture bytes unchanged. These
are application lifecycle checks, not new terrain/numerical acceptance.
The final dev repository gate passed 153 CTest cases with zero failures;
20 opt-in windowed cases were skipped. The default Python host test's optional
NumPy case was skipped there, then all six host cases passed separately in the
retained NumPy environment. See `final-regression-2.log`,
`final-regression-2.xml` and `local-gui-lifecycle/result.json`.

Evidence is under `outputs/fluid/backend-integration-v2-20261004-wlELKz/`:
`native-continuous-soak-2/result.json`, `native-gui-failure-3/result.json`, and the
successor `integration-verdict.json`. Earlier failed audits/runs and the earlier
incomplete `protocol.json` checkpoint are retained, not rewritten as successes.
Static overview/collection PNGs validate bit-exact bed/depth/velocity uploads
with zero hydraulic dispatches in Cubey. Automated captures do not replace the
deferred human GUI review, and extreme uniform study rainfall is not calibrated
hydrology. Neither native conserved tracer nor a complete native flux ledger
is published. Switching backends requires a new session; numerical states are
not transferred between methods.

### Opt-in live external SynxFlow viewing V1

`--fluid25d-stream` reads an atomically published growing prefix from a separate
foreground launcher. Cubey remains a Vulkan-only presentation application: it
does not link CUDA, dispatch hydraulics, or share GPU allocations with SynxFlow.
The installed pinned solver is unchanged; this is live viewing of finite runs,
not interactive rainfall control or indefinite simulation.

```sh
rtk proxy python3 projects/fluid/fluid_25d/live_synxflow_session_v1.py \
  --baseline-case outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ/cases/mountain-rain-on-14400s \
  --out outputs/fluid/my-new-live-session \
  --python outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python
```

After its first `stream.json` appears, attach in another terminal:

```sh
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-stream outputs/fluid/my-new-live-session \
  --fluid25d-recording-camera overview
```

The default is paced viewing at 60 physical seconds per wall second. Optional
`--fluid25d-stream-follow-latest` follows the latest complete imported state and
may skip saved states. The GUI distinguishes native computation, publisher
lifecycle, newest native/available times, displayed time, and import lag.
Space pauses **viewing only**. Seeking/restart affects only the viewing timeline.
Closing the GUI detaches; cancel the foreground launcher to stop only its owned
child. When the finite run finishes, it holds the last state without looping.

The launcher clones audited inputs into a fresh directory without copying old
outputs/results as evidence of the new run. It preserves terrain, forcing,
friction, boundaries and native output cadence. A successor-export barrier
prevents reading an unfinished `h`/`hUx`/`hUy` triple; the last frame is withheld
until successful child exit. Payloads are written before the atomic stream
manifest, hashed, and immutable once published. The consumer rejects changed
session identity, revision regression, altered metadata/history, and corrupt
payloads. An early failure before frame0 retains `session-result.json` instead
of fabricating a frame or weakening the nonempty-prefix contract.

Static PNG snapshots can read a live prefix; live headless video is rejected.
Convert the finalized `case/` with the existing converter for portable recorded
playback/video. Partial/failing session evidence remains available but is not
marked as completed-case acceptance. No global installation is performed.

`run_live_view_acceptance_v1.py --out <fresh-directory>` is an explicit native
and windowed acceptance runner, not part of ordinary tests. It retains failures,
compares native fields to the frozen mountain reference and binary frames to
the unchanged converter, and measures actual run overlap, publication latency,
and matched/concurrent viewer profiles. Automated command-record timing and
scoped window captures do not imply human visual acceptance or monitor scanout.

V1 native evidence is retained at
`outputs/fluid/native-live-view-v1-20261003-0612Z`: both unchanged 512x512,
30 m, four-hour mountain cases and a paced rain-on repeat match all 723 native
field files and all 241 independent-converter payloads per run. The two
follow-latest views displayed 86/82 nonzero-time states during actual native
calls; publication-to-command-record p95 was 0.160/0.162 seconds. Frame0
initialization is separate, and paced viewing intentionally shows older states.

Matched completed-prefix live/recorded p95 frame time was 1.010x. Concurrent
CUDA raised the paced viewer from 0.653 ms on an idle GPU to 1.116 ms, failing
the frozen 1.2x idle-GPU gate (`acceptance-failure.json` remains unchanged).
The user explicitly accepted V1 with this shared-GPU limitation retained;
do not describe the original matrix as all-gates-passed. ASCII publication
also queued up to 16.9 seconds behind an observed successor export. These are
local RTX5070Ti observations, not a release or calibrated-hydrology benchmark.
Human GUI acceptance remains a separate review.

The matched CUDA control is retained at
`outputs/fluid/native-live-view-gpu-control-v1-20261003-0632Z`: recorded p95
was 1.120 ms versus live 1.116 ms (0.997x), supporting a shared-workload
explanation rather than large bridge overhead. Its 172-frame prefix ended
`cancelled`, with the stock native call already returned normally and all
723 native fields still matching the baseline. Earlier failed controls remain
retained; do not reuse them as terminal-state acceptance.

`run_live_view_gpu_control_v1.py` is an additional opt-in, finite diagnostic:
it views the recorded fields with the same stock CUDA/publisher workload.
It closes only its own window after ten wall seconds and cancels its importer
only after the normal native call returns. A cancelled partial stream in that
control is not completed-stream acceptance; native fields, captures, timings,
and any failed attempts are retained. Cancellation preserves the last
published prefix immediately rather than draining a backlog first.

### Recorded playback contract

`--fluid25d-recording` selects a separate presentation-only application path.
It displays saved SynxFlow depth and depth-integrated momentum over the exact
numerical bed used by that run. It does not create or dispatch hydraulic reset,
forcing, CFL, or solve pipelines. Existing virtual-pipes and finite-volume
modes/defaults remain unchanged. This is **recorded playback**, not a live
external backend, solver port, calibrated hydrology, or native conservation
certificate.

Convert a completed case without importing SynxFlow or requiring CUDA:

```sh
rtk proxy python3 projects/fluid/fluid_25d/convert_synxflow_recording_v1.py \
  --case path/to/completed-native-case --out outputs/fluid/new-recording
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-recording outputs/fluid/new-recording \
  --fluid25d-recording-speed 60 --fluid25d-motion-markers
```

The converter refuses an existing destination. It requires matching case spec,
frozen protocol, case-result input audit, original `DEM.asc`, native `z.dat`,
and a complete regular `h`/`hUx`/`hUy` ASC series. It supports the audited
native `fall` bed-boundary serialization, not arbitrary SynxFlow exports.
Portable `recording.json` uses versioned, hashed, relative-path planar
little-endian float32 payloads: `h`, `qx`, and world-Z `qz=-hUy` retain raster
row order. Displayed velocity is `q/h`, not momentum mistaken for velocity.
Invalid/nonfinite/negative states, zero-depth nonzero momentum, bad cadence,
and integrity/path failures reject. The reader caches at most three frames
and loads windowed states off the UI thread; I/O waits hold the playhead.

Optional `protocol.rainfall_history` contains strictly increasing
`[time_s, rate_m_per_s]` knots from zero to the recording duration. Conversion
requires agreement with the case protocol, pre-solver audit and hashed native
rainfall input. Older constant-rain recordings remain supported. The UI reports
On/Tapering/Off, rate and cumulative **scheduled** rain at the actual displayed
saved state, not a pending requested time. Rain-event seek buttons pause and
reset visual history; they do not change physics. A canonical terminal
`h_max_<duration>.asc` is hashed auxiliary provenance, never a playback frame.

The bounded rain-recession study is retained in
`outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ/index.html`. Both four-hour
runs exactly reproduce the accepted 120 mm/h first two hours. One then tapers
to zero in one minute; the other keeps raining. After shutoff, total storage
falls about 51% while fixed B20 storage grows about 17% as upstream water
arrives. None of the three frozen depression masks meets the persistent quiet,
near-level patch criterion: recession and moving collection, not a verified
calm lake. The gallery includes matched recorded clips, rain-event captions,
raw-state review and local launch commands. Human GUI review is separate.

The original DEM is retained separately. Rendering uses the native numerical
bed because `%g` serialization differs from original terrain by up to about
5 mm in the reviewed mountain. There is no terrain smoothing, channel carving,
water amplification, or physical-field interpolation. Samples are cell centers
centered in world X/Z; cell-area domain size is `width*dx`, whereas the mesh
sample-to-sample span is `(width-1)*dx`.

Space plays/pauses. Seeking, previous/next, and R restart pause and clear visual
history. The end holds the last state without looping. The GUI shows requested
playhead and actual saved time, explicit recorded labels, and fixed scales.
Fields are held between saves. `overview`, `runoff`, and `collection` cameras
are observation targets for the reviewed mountain (collection is previously
surveyed B20), not sources/sinks. Rain supplies the entire map; native edge
`fall` conditions are distinct from Cubey's legacy boundary model.

Composite attenuates thin rain film; Water Isolation reveals all wet cells.
3D depth uses a logarithmic `0.01..10 m` palette; the 2D depth map is brighter
for deeper water on the same scale and preserves physical X/Z aspect. The
speed map spans `0..15 m/s`. Arrows use fixed-grid wet-aware neighborhood
velocities with color/length spanning `0.025..4 m/s`, saturated above 4; maximum
saved speed is also reported numerically. Optional dots/trails and scalar cues
are approximate visual advection in held saved velocities, not native parcels
or conserved dye. Sparse video intervals cap only visual catch-up at 60 seconds
per output frame, without changing saved physical fields.

Exact saved-time stills and indexed video have independent controls:

```sh
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d --headless \
  --fluid25d-recording outputs/fluid/new-recording \
  --fluid25d-recording-time-seconds 7200 \
  --fluid25d-recording-camera collection \
  --fluid25d-recording-gpu-validation --output outputs/fluid/recorded-final.png
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d --headless \
  --fluid25d-recording outputs/fluid/new-recording \
  --capture video --frames 121 --fps 8 \
  --fluid25d-recording-frame-interval-seconds 60 \
  --output outputs/fluid/recorded-playback.mp4
```

GPU validation checks bed, both depth buffers and derived velocity bit-for-bit
after presentation, not native conservation/CFL. Hydraulic/legacy controls
reject instead of silently doing nothing. Automated coverage includes
converter orientation, reader integrity/bounded cache, playback/end behavior,
CLI separation, dry/signed-asymmetric GPU states, and multi-slot video updates.
Generated local review evidence is under
`outputs/fluid/native-recording-viewer-v1-20261003-EWKAhM`; its gallery,
captures, clips and launch command retain provenance. Human GUI acceptance
remains pending.

### Native rain-intensity comparison

`run_native_rain_intensity_v1.py` is a separate, frozen-input experiment on the
same unchanged 512x512, 30 m mountain. It reuses the sealed two-hour 12 mm/h
baseline and prepares continuous 48 and 120 mm/h dry-start cases. Native
serialized inputs must match the baseline byte-for-byte except the rain
schedule, before the released SynxFlow solver can run. `--prepare --phase all`
does not run hydraulics; `--run --phase all --go` requires the pinned existing
native Python environment and stops before 120 mm/h if the 48 mm/h case fails.
Existing solvers, defaults and terrain are unchanged.

Protocol, retained native cases, independent saved-field checks, matched Cubey
playback and launch commands are under
`outputs/fluid/native-rain-intensity-v1-20261003-bXgHXx/`. Its `index.html` is
the review entry point. Rain-normalized concentration distinguishes runoff
from direct rain film; storage alone is not proof of a quiet lake. This is
uncalibrated forcing sensitivity, not storm forecasting, a live Cubey backend
or a native face-flux conservation certificate. Human GUI review is separate.

This runner is retained as historical, protocol-bound study tooling. Its frozen
converter hash predates the current rainfall-history adapter, so its workspace
check now fails closed with the current converter. Replaying that experiment
requires its archived matching sources; do not relax the frozen identity checks
or treat its historical test counts as current validation. Optional study suites
also require their own scientific Python dependencies, unlike the standard-library
converter and live-publisher tests registered in CTest.

### River V0

The first numerical fixture is an authored upland catchment:

```text
source -> terrain-guided channel/basin -> explicit downstream sink
```

River V0 includes:

- deterministic terrain and scenario generation;
- explicit `terrain-case` ingestion from an immutable
  `cubey.terrain.heightfield.v1` raster crop;
- opt-in, uniform `rain-pulse` and `sheet-release` terrain-case forcing for
  matched discovery auditions;
- one or more depth-field sources and sinks;
- explicit closed or outflow-only perimeter faces, with boundary discharge
  recorded separately from explicit sinks;
- virtual-pipes shallow-water evolution;
- water depth, surface height, outgoing face flux, velocity, and wet/dry state;
- explicit source/sink/boundary-outflow volume accounting;
- a GPU-resident steady-state solve with no per-frame readback in normal use;
- an implicit oblique grid (no uploaded mesh) with opaque terrain, translucent
  premultiplied-alpha water, depth testing, restrained lighting, and a
  deterministic render-only advected flow cue;
- a Flow Inspection-only, fixed-grid GPU quiver field. Arrow angle shows
  local downstream velocity, while restrained length and brightness show
  speed; the arrows are samples, not waves, foam, water mass, or particles;
- an opt-in Transport Inspection surface for eligible finite-volume demos.
  A one-scalar conservative dye pulse enters at SOURCE and moves with accepted
  water face flux. Source-outlet cases remove dye at their sink; the hillside
  has no prescribed drain and can export only through crop edges. This is a
  reading aid, not a particle system or a general tracer framework;
- top-down terrain, depth, surface, flow magnitude/direction, and wet/dry
  diagnostics as an explicit alternate presentation mode;
- opt-in headless CPU/GPU fixtures that read back only at their final evidence
  point;
- opt-in headless profile diagnostics at the requested common diagnostic interval,
  including hydraulic-state fingerprints and an optional independent committed
  water/tracer arithmetic audit for finite-volume investigations.

It deliberately excludes erosion, sediment, interactive editing, spray,
breaking waves, periodic or inflow-capable outer boundaries, water-table or
infiltration state, and high-order flood modeling claims. Uniform audition
rainfall is a bounded source-field protocol, not climate or a flooding product.
Outflow-only perimeter faces are a bounded numerical primitive, not a flooding
product. Flooding and an interactive terrain/water toy are later products, not
hidden requirements of River V0.

The windowed visual cue is a project-local ping-pong scalar field at the
fluid-grid resolution. Reset restores the same broad seeded field; an accepted
outer fixed step backtraces it with the final physical velocity after all
solver substeps, while a rejected finite-volume status leaves it unchanged. It
is not solver state and never enters depth, momentum, CFL, ledgers, or
oracle/diagnostic readback. Pausing performs no cue update; calm water uses
the stable base shade rather than a time-driven pattern.

Flow Inspection has a separate project-local quiver-state buffer. Every state
element owns one deterministic, fixed anchor in a regular 96 by 48 lattice on
the default 256 by 128 product grid and the 128 by 64 source-to-outlet demo;
the compact demo therefore has about 1.3 cells between anchors, while the
product grid has about 2.7. Small fixtures reduce that count rather than
producing duplicate anchors. A three-by-three wet/finite-aware neighborhood
average samples the published physical velocity around each anchor after an
accepted outer fixed step. Direction, strength, and opacity smooth in
simulation time, but anchor coordinates never advect, reseed, or retire.
`0.025 m/s` is the visibility threshold, intentionally above the retained
initial terrain-sheet maximum, so the dry bed, lake at rest, and honest initial
terrain state remain arrow-free. Arrow silhouette length follows local lattice
pitch (about 58--72% of the pitch), while blue-to-yellow color and length use
the fixed `0.025..0.80 m/s` physical range; this keeps the dense demo field
readable without making larger-grid arrows disappear. Current dry or
nonfinite data hides an arrow immediately; calm valid water fades it through
the same presentation-only state. Reset samples the current parity-selected
field, allowing an established paused flow to be read without a host readback.
A sticky finite-volume status is a strict no-write boundary for reset and
update. This state never enters solver descriptors, wet/dry policy,
diagnostics, profiler diagnostics, or CPU/GPU oracle readback.
Composite, Water Isolation, and top-down Diagnostics never dispatch or draw
the quiver. Flow Inspection suppresses Composite's broad scalar-cue highlight
so its fixed arrows are the only direction language. Windowed Flow Inspection
includes optional work in the existing presentation command span; explicitly
selected headless Flow Inspection updates after each solver record, outside
the solver-only timestamp/oracle lane. Its initial reset is recorded before
the first headless solver step, so the first capture remains the truthful
unanimated initial state; this changes only presentation state and does not
change solver dispatches or timestamps.

### Source/drain cell highlights

The explanatory catchment scenes use translucent cubes instead of location
rings. Each positive source/sink-rate cell gets one cell-wide cube centered
at its own terrain height: green input, amber explicit removal. Cube size is
fixed in render metres; terrain height scaling moves the center but does not
stretch the cube. Terrain depth testing hides buried portions. Cube height
is a visual region marker, not water depth or a physical emitter volume.
Overlapping sides between adjacent same-kind cubes are omitted, retaining
only exposed height steps. Markers remain static across pause/reset and never
enter hydraulic descriptors or state. Default River V0 is unchanged.

Open crop boundaries and expected-exit observation windows are not sinks and
do not get amber cubes. Retained older captures/study notes still show rings;
those remain historical evidence, not captures of this renderer.

### Opt-in source-to-outlet scene

`source-outlet-demo` is a separate authored explanation scene, not a change to
the River V0 virtual-pipes fixture or its default camera. It is meaningful only
when launched explicitly with the opt-in finite-volume solver. On the
one-metre `128x64` grid, green SOURCE and amber DRAIN cube patches cover the
forcing strips near columns `14` and `110`. The channel has two broad S-bends, variable width,
and one constriction. Its bed falls `0.45 m` over the 96 m route, while the
selected lower bank crest is about `3.00 m` above the centerline bed. Broad
smoothstep banks avoid a near-vertical trench. Initial water is seeded within
the discrete bank span with its surface `0.30 m` below the lower crest, so the
river is deep and near-bankfull at reset without claiming that source water
has already traveled to the outlet.

This is deliberately stylized authored relief, not an imported real-world DEM.
The scene-only height/slope tint and render-only relief scale make the dry
banks legible without changing numerical heights. The source and sink each have
a four-column forcing strip confined to already-wet, in-bank cells; each strip
is normalized to `2.00 m3/s` of physical throughput. Actual removal is still
limited by available water and should be read from the ledger. The short tan
shoulders behind the markers rise `3.00 m` upstream and `3.30 m` downstream
over four cells to contain the deep-water closed-domain tails. They are
stylized end caps, not physical intake structures or a stage-controlled open
boundary. The home camera frames both endpoints; user orbit and zoom remain
available. Ring words live in the control-panel legend.

Use the compact one-metre `128x64` presentation grid and select the finite-
volume solver explicitly; this does not alter the global `river-catchment` or
virtual-pipes defaults:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d \
  --grid-width 128 --grid-height 64 \
  --fluid25d-scenario source-outlet-demo --fluid25d-solver finite-volume
```

Append `--fluid25d-catchment-view flow-inspection` to use the optional
fixed-grid velocity-field reading mode. The arrows show local velocity, not
the path of one parcel. For transport, inject a half-open dye pulse while the
hydraulic source stays active. This command advances 2,400 simulated seconds in
2,400 frames and encodes them as a 40-second video:

```sh
mkdir -p outputs/fluid/source-outlet-review
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --capture video \
  --frames 2400 --fps 60 --width 1280 --height 720 \
  --grid-width 128 --grid-height 64 \
  --fluid25d-scenario source-outlet-demo --fluid25d-solver finite-volume \
  --fluid25d-catchment-view transport-inspection \
  --fluid25d-fixed-delta-seconds 1 --fluid25d-substeps 32 \
  --fluid25d-dye-pulse-start-seconds 300 \
  --fluid25d-dye-pulse-duration-seconds 60 \
  --profile-output outputs/fluid/source-outlet-review/profile \
  --profile-diagnostics --profile-diagnostic-interval 1 \
  --output outputs/fluid/source-outlet-review/transport.mp4
```

Read the magenta packet alongside the profile CSVs, not as stand-alone proof
of flow. `fluid_25d.river_cross_section` reports actual discrete bank crests,
wet widths, depth, approximate cell-centred discharge, and local velocity at
five stations. `fluid_25d.river_spatial` partitions every cell into before
SOURCE, in-bank route, off-bank route, or after OUTLET; it distinguishes a
small closed-domain endpoint tail from lateral overbank flow.
It also reports route-wide representative freeboard and centerline-depth
min/mean/max values across all 97 source-to-outlet columns, helping reveal
gaps that the five named stations may miss.
`fluid_25d.water` and `fluid_25d.tracer` provide source/sink/boundary ledgers
and conservation residuals. The diagnostic `0.01` dye-concentration threshold
marks a material visible front; it is not the injected concentration. Capture
frame `fN` is the state after `N` simulated seconds: `f300` precedes dye,
`f301` is the first dyed step, and `f360` is the last.

The [reviewed deep-channel run](../../../outputs/fluid/deep-river-v1-20260923/report.md)
has zero status flags, boundary loss, and route off-bank water across all
2,400 frames. At `f900`, all 97 route sections are wet with 0.181–0.499 m
representative freeboard and 2.500–2.819 m centerline depth; interior section
discharge remains about `2 m3/s`. Before-SOURCE and after-OUTLET water settle
to `31.275` and `37.720 m3`. Material dye passes the OUTLET marker at `f1384`,
and the explicit sink has captured `118.420 m3` (`98.68%`) of the `119.999 m3`
pulse by `f2400`. The reported water conservation residual is `+4.375 m3`
against `2806.518 m3` stored water at `f2400`; the full report leaves this
ledger discrepancy visible rather than assigning a cause. The larger section
slows the dye compared with the historical shallow scene, so `f900` is no
longer a sufficient transport horizon.

`run_transport_readability_v1.sh` and its linked V1 note preserve an earlier
historical protocol and thresholds; use the command above for this current
scene, not the V1 runner as an acceptance gate.

### Opt-in sustained-headwaters control

`sustained-headwaters-demo` is a separate, authored finite-volume control for
dynamic flow rather than a pre-filled river. Its 65×33 grid has 4 m cells and
starts completely dry. Two marked 3×3 headwater patches each supply 0.25 m³/s
continuously; their banked reaches join into one trunk. Only a three-face east
aperture is open, with outflow-only boundary behavior. There is no explicit
water sink. Green cube patches mark both inputs; the open east edge is not
highlighted as a drain. The 4× visible relief and terrain tint are render-only.

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d \
  --grid-width 65 --grid-height 33 --fluid25d-cell-size-m 4 \
  --fluid25d-scenario sustained-headwaters-demo --fluid25d-solver finite-volume \
  --fluid25d-fixed-delta-seconds 1 --fluid25d-substeps 32
```

The [V1 result note](../../../docs/notes/fluid-25d-sustained-headwaters-v1.md)
shows the two wet fronts joining, measured late outlet discharge, station
velocities, a passive dye packet crossing the same route, and the remaining
natural-terrain resolution boundary. Run
`projects/fluid/fluid_25d/run_sustained_headwaters_demo.sh` for the two
dry-to-mature videos, dye-transport video, stills, profiles, and GPU/CPU
comparison. The control remains a shallow stream scene, not a near-bankfull
or naturally discovered river.

The opt-in resolution study keeps the same 256×128 m physical reach and
0.25 m³/s per source while allowing `129x65` at 2 m and `257x129` at 1 m in
this scenario only. The original 4 m arrays and the project defaults are
unchanged. Run `python3 projects/fluid/fluid_25d/run_sustained_headwaters_resolution_v1.py
--mode resolution` to compare the three grids, or replace the mode with
`--mode damping --damping-grid-m 1 --video`
to compare 0.15, 0.075, and 0.05/s momentum damping at 1 m. The
[resolution and speed note](../../../docs/notes/fluid-25d-headwaters-resolution-speed-v1.md)
links the captures, timing and ledger evidence, and the current validation
boundary. These are study controls, not calibrated flow settings.

An additional headwaters-only `--fluid25d-headwaters-source-scale` multiplies
both continuous source rates while leaving their footprint, terrain, and
outflow aperture unchanged. The default `1` preserves the original scene.
The [supply and speed study](../../../docs/notes/fluid-25d-headwaters-supply-speed-v1.md)
compares 1×/2×/4×/8× supply on the 1 m grid and one combined lower-damping
case. Source rate makes the channel fuller and its initial front earlier, but
does little for dye travel speed on its own. Use `python3
projects/fluid/fluid_25d/run_sustained_headwaters_resolution_v1.py --mode
supply --source-scales 1 2 4` for the bounded first sweep; these opt-in
controls do not change the project defaults.

The [bankfull and spill study](../../../docs/notes/fluid-25d-headwaters-bankfull-v1.md)
adds local sampled bank/freeboard sections and a separate whole-grid authored-
corridor spill indicator. With the same 1 m terrain and 0.05/s damping, 24×
supply is the fullest tested no-indicated-spill opt-in review point; 28×
already spills near the outlet while the branches remain below bankfull.
This is a bounded authored-scene result, not a default or general containment
certificate.

### Opt-in immutable mountain source/outlet scene

`mountain-source-outlet-demo` is a separate finite-volume-only product
demonstration on one pinned, immutable `mountain-valley-1` crop. It does not
promote the neutral `terrain-case` audition protocol, alter River V0, or edit
the imported elevation. Its 256 by 128 native 30 m crop is pinned at
`(1536,1664)` by both the input elevation SHA-256
`2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48` and the
transformed crop SHA-256
`9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6`.

The green cubes cover the 81-cell source region centred at `(8,60)`, whose total
configured input is `0.75 m3/s`. Amber cubes cover the actual explicit drain:
the three reviewed low cells `(232,127)`, `(229,126)`, and `(231,126)` remove a
total configured `0.75 m3/s`. All outer faces are closed. The route is a
reviewed, radius-four prewetted initial-water corridor; it makes the whole
source-to-outlet story readable at reset, but is not a claim that one water
parcel, a surveyed river, or a provenance record follows that exact path.

The home camera frames the full 6.97 km endpoint route. A mountain-only
render-space relief scale and elevation/slope palette expose the valley and
ridges; they do not alter elevation buffers, water state, source/sink masks,
the finite-volume solve, or any other scene. Composite is the terrain-and-water
overview, Water Isolation makes the wet corridor easier to see, and Flow
Inspection overlays the existing fixed 96 by 48 stable velocity samples. The
small arrows show local direction and speed, not water particles or a tracked
parcel.

Launch it directly with the pinned source and the recommended 2 s outer step
and eight solver substeps:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-scenario mountain-source-outlet-demo \
  --fluid25d-solver finite-volume \
  --terrain-heightfield cache/terrain/sources/v1/presets/mountain-valley-1 \
  --fluid25d-fixed-delta-seconds 2 --fluid25d-substeps 8
```

For a reproducible remote-review pack (reset, mature Composite, Water
Isolation, Flow Inspection, a 120-frame CPU/GPU oracle smoke, and 600-frame
profiles), run:

```sh
projects/fluid/fluid_25d/run_mountain_source_outlet_demo.sh \
  outputs/fluid/mountain-source-outlet-v1-20260921
```

The 600-frame profile represents 1200 s. Expect zero finite-volume status and
boundary ledger, late-window source and actual sink removal near `0.75 m3/s`,
and bounded stored water. The runner writes the exact capture hashes, pinned
input identity, metrics, and measured late-window rates into its output
directory; use those results rather than assuming the configured sink rate was
fully realised.

## Numerical contract

All quantities use physical units:

- terrain height, water depth, and cell size are metres;
- source and sink fields are depth rates in metres per second;
- face fluxes are discharge rates in cubic metres per second;
- the water ledger is cubic metres;
- `fixed_delta_seconds` is the simulation-frame duration and
  `simulation_substeps` divides that duration into fixed solver steps;
- `minimum_wet_depth_m` is a numerical wet/dry threshold: cells at or below it
  do not retain or originate outgoing flux.

For a cell with depth `h`, bed height `b`, and surface height `eta = b + h`,
the oracle advances a persistent directed discharge for each neighboring pipe:

```text
pipe_area  = cell_size * cell_size
f_next     = max(0, exp(-damping * delta_seconds) * f_previous
                    + delta_seconds * pipe_area * gravity
                    * (eta - eta_neighbor) / cell_size)
```

The four fluxes are owned by the source cell. `pipe_area` is deliberately a
project-local cell-area choice for this first oracle; it is not a public tuning
option. A higher neighboring surface decelerates retained directed flux before
the nonnegative clamp. Before updating depth, total outgoing discharge is
scaled so that the cell cannot export more than its available volume during the
current substep. The depth update then applies

```text
h_next = h + delta_seconds / cell_area * (incoming_flux - outgoing_flux)
```

with source and sink depth rates converted to volume by multiplying by cell
area. Outer faces are closed in River V0; the explicit sink is the only
intentional removal path. The positivity limiter and finite-value checks keep
the water field nonnegative and finite.

This is a compact virtual-pipes reference scheme. It is retained as the
product default while the separate well-balanced finite-volume comparison is
audited on the same immutable scenarios.

### Opt-in finite-volume comparison

The audition trigger above is exercised by a separate CPU/GPU comparison, not
by a silent change to River V0. It advances the depth-integrated Saint-Venant state
`U = (h, h*u, h*v)` with a first-order Cartesian local Lax-Friedrichs/Rusanov
flux. At every face it uses the hydrostatic reconstruction of Audusse et al.,
"A Fast and Stable Well-Balanced Scheme with Hydrostatic Reconstruction for
Shallow Water Flows," *SIAM J. Sci. Comput.* 25(6), 2004,
[doi:10.1137/S1064827503431090](https://doi.org/10.1137/S1064827503431090):

```text
h*_left  = max(0, h_left - max(0, z_right - z_left))
h*_right = max(0, h_right - max(0, z_left - z_right))
U*  = (h*, h* u, h* v)
F̂   = 1/2 (F(U*_left) + F(U*_right))
       - 1/2 a (U*_right - U*_left)
a   = max(|u_n,left| + sqrt(g h*_left), |u_n,right| + sqrt(g h*_right))
```

Each adjacent cell receives its own hydrostatic pressure correction
`g/2 * (h² - h*²)` in the face-normal momentum flux. This preserves a
constant free surface across uneven beds, including a wet/dry shoreline,
while internal mass flux remains exactly equal and opposite. Closed faces use
a reflected normal-velocity ghost; an opened P2 face uses a dry ghost at the
edge cell's bed elevation, cannot add external water, and contributes only its
outward mass flux to the existing boundary ledger.

Sources and sinks are applied before transport. A source adds no momentum; a
sink removes the same fraction of local depth-integrated momentum as water.
After conservative transport, the existing exponential `flow_damping` is an
explicit linear momentum drag and never changes water mass. Apart from a
`1e-7 m` final float residue, a non-finite or negative depth fails closed; the
comparison does not use the virtual-pipes export clamp.

Every finite-volume substep checks the conservative unsplit two-dimensional
CFL number before mutating persistent state:

```text
CFL_2D = delta_seconds / cell_size
         * (max(|u| + sqrt(g h)) + max(|v| + sqrt(g h))) <= 0.45
```

`0.50` is the hard ceiling for this first-order Cartesian form; the CPU oracle
rejects any step above the `0.45` target. The GPU path performs the identical
global-max criterion through a status prepass/finalize/candidate/commit
sequence. Candidate writes only inactive `h, hu, hv` plus isolated velocity
and ledger-delta buffers; after the global status barrier, commit accepts all
candidate cells (leaving candidate `h, hu, hv` in that inactive destination)
or copies the source `h, hu, hv` into the next parity unchanged, leaving common
velocity and cumulative ledger untouched.
Candidate validates each prospective cumulative ledger before commit, so
commit has no late partial-write error path. Error flags remain sticky until
explicit solver reset while only the two CFL-max scratch values are cleared per
substep; GPU-oracle validation and opt-in profile diagnostics reject any
nonzero flag. Ordinary headless finite-volume capture also reads sticky status
once at final capture; windowed comparison runs stay GPU-resident and require
an explicit diagnostic/readback mode to surface a device flag to the host.
This is comparison evidence, not product-ready evidence: the untouched-terrain
V2 auditions retain a ranked shortlist, but no case advances until a concrete
water story and a presentation-validity pass justify promotion.

## Deterministic fixtures

The project-local scenario generator provides three product-facing analytic
fixtures:

- `dry-bed`: uneven terrain with zero initial water and no rates;
- `lake-at-rest`: uneven terrain with a constant free-surface height and no
  source or sink;
- `river-catchment`: a downhill quadratic channel seeded with a thin water path,
  one source near the upstream end, and one initially dry downstream sink.

The CPU tests cover deterministic generation, configuration parsing and
validation, virtual-pipes regression behavior, and finite-volume dry uneven
beds, wet/dry lake-at-rest preservation, a symmetric flat-bed dam break,
open-boundary drainage, source/sink/boundary ledger reconciliation, and
fail-closed CFL rejection.

## Configuration

River V0 uses a project-owned Config V2 facade. It composes the shared 2D grid
schema and currently exposes:

- `--grid-width`, `--grid-height`, and `--grid-size`;
- `--fluid25d-view catchment|diagnostics` (top-level surface selector; default:
  `catchment`);
- `--fluid25d-catchment-view composite|water-isolation|flow-inspection|transport-inspection`
  (windowed or headless catchment presentation; default: `composite`; cannot
  be combined with `--fluid25d-view diagnostics`);
- `--fluid25d-presentation-time-scale <0.125..8>` (windowed-only; defaults to
  `1`, with `4` and `8` useful for review playback). Fast requested playback is
  best-effort: slow or stalled rendering can hit the four-step catch-up cap and
  drop excess backlog;
- `--fluid25d-solver virtual-pipes|finite-volume` (default: `virtual-pipes`;
  finite-volume is an opt-in numerical comparison);
- `--fluid25d-scenario dry-bed|lake-at-rest|river-catchment|source-outlet-demo|sustained-headwaters-demo|mountain-source-outlet-demo|terrain-case|boundary-drain-fixture`;
- `--terrain-heightfield <manifest-or-directory>` (required by `terrain-case`);
- `--fluid25d-terrain-crop-x` and `--fluid25d-terrain-crop-z` (native sample
  indices; the configured grid dimensions define the crop extent);
- `--fluid25d-terrain-water-protocol none|rain-pulse|sheet-release` (default:
  `none`; terrain-case only);
- `--fluid25d-rainfall-rate-mm-per-hour` and
  `--fluid25d-source-active-duration-seconds` (required together by
  `rain-pulse`);
- `--fluid25d-terrain-thin-water-composite` (terrain-case Composite-only,
  opt-in render attenuation for water shallower than 5 cm; solver wetness,
  water mass, diagnostic views, and default rendering are unchanged);
- `--fluid25d-sheet-depth-m` (required by `sheet-release`);
- `--fluid25d-cell-size-m`;
- `--fluid25d-fixed-delta-seconds`;
- `--fluid25d-substeps`;
- `--fluid25d-gravity-m-per-s2`;
- `--fluid25d-flow-damping-per-second`;
- `--fluid25d-headwaters-source-scale` (positive finite, opt-in for
  `sustained-headwaters-demo` only);
- `--fluid25d-minimum-wet-depth-m`;
- `--fluid25d-dye-pulse-start-seconds` and
  `--fluid25d-dye-pulse-duration-seconds` (required together for the opt-in
  finite-volume `source-outlet-demo` or `sustained-headwaters-demo` Transport
  Inspection surface; the
  half-open interval is evaluated on completed fixed simulation steps);
- `--debug-view terrain|depth|surface|flow|direction|wet-dry`;
- `--profile-output`, `--profile-warmup-frames`, `--profile-diagnostics`, and
  `--profile-diagnostic-interval` (profile diagnostics are headless-only and
  deliberately opt into readback); solver rows are not full-frame or FPS
  measurements;
- `--fluid25d-gpu-oracle-validation` (headless-only; performs final-state
  readback against the CPU oracle).

Normal headless capture keeps the simulation GPU-resident. Finite-volume
capture additionally reads its sticky solver-status word once at final capture
and fails if the solver rejected a substep:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --width 512 --height 256 --output fluid-25d.png
```

The normal headless command produces the same static oblique Composite
catchment framing as the windowed default. To compare the reading aids in
reproducible captures, select the catchment presentation explicitly:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-catchment-view water-isolation --output fluid-25d-water-isolation.png

build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-catchment-view flow-inspection --output fluid-25d-flow-inspection.png
```

Virtual-pipes does not read back simulation state;
finite-volume reads only its final sticky status. To capture a top-down
numerical diagnostic instead, select it deliberately:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-view diagnostics --debug-view flow --output fluid-25d-flow.png
```

The two focused evidence lanes are intentionally opt-in:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 12 \
  --grid-width 12 --grid-height 8 --fluid25d-scenario lake-at-rest \
  --fluid25d-gpu-oracle-validation --output fluid-25d-lake.png

build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 24 \
  --grid-width 24 --grid-height 9 --fluid25d-scenario river-catchment \
  --fluid25d-gpu-oracle-validation --output fluid-25d-river.png
```

An imported terrain case begins dry with no source or sink rates. Its cell size
is resolved to the source's native sample spacing unless
`--fluid25d-cell-size-m` is supplied explicitly; an explicit conflicting value
is rejected. The application reports a stable identity containing the source
elevation SHA-256, transformed crop SHA-256, crop coordinates and extent, and
native spacing. Terrain crops are
asset inputs only: they do not invoke Python/Torch or the Terrain application,
and they do not add water-table, candidate-specific boundary policy, erosion,
or sediment behavior. Terrain-case `none` remains dry and closed. The two
audition protocols construct complete uniform fields and open every outward
perimeter face; they never paint a channel, move an outlet, or reshape the
terrain.

The checked-in V1 runner preserves the frozen virtual-pipes negative baseline:

```sh
projects/fluid/fluid_25d/run_terrain_water_audition.sh
```

The separate finite-volume-only V2 runner keeps the same immutable five crops,
but uses a shared 1,200-second physical-horizon rain/sheet protocol:

```sh
projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh
```

Both write ignored evidence under `outputs/fluid`. V2 supports an explicit
`CASE_FILTER` only for bounded pilots; it cannot change crop transforms,
forcing, or perimeter policy. Profile metrics include solver status, wet and
active-flow coverage, stored water, depth, speed, slow/pooled wet fraction,
cumulative source/sink/outflow ledger totals, and the visible conservation
residual. “Slow/pooled” means a wet cell at or below `0.02 m/s`; this is a
reporting threshold, not a solver control. The exact V1/V2 evidence and
shortlist/no-promotion decision live in
[`docs/notes/fluid-25d-terrain-water-audition-v2.md`](../../../docs/notes/fluid-25d-terrain-water-audition-v2.md).

The separate finite-volume rain dynamics study compares one neutral and one
visibility-stress tier across three immutable terrain crops. Its diagnostic
evidence and no-promotion result live in
[`docs/notes/fluid-25d-dynamics-study-v1.md`](../../../docs/notes/fluid-25d-dynamics-study-v1.md);
rerun it with
`projects/fluid/fluid_25d/run_rain_dynamics_study_v1.sh` when refreshing that
study.

The follow-up [rain catchment study](../../../docs/notes/fluid-25d-rain-catchment-study-v1.md)
separates render-only thin-water legibility from a pinned natural-catchment
pilot. For headless RainPulse profiles, `fluid_25d.boundary` now reports the
existing cumulative perimeter outflow in 16 bins per side plus separate corner
and non-edge totals; these metrics do not alter the solve. The candidate
shows distributed runoff, not a promoted single-outlet scene. Its reproducible
scripts are `terrain_rain_thin_water_ab_v1.py` and
`terrain_rain_catchment_pilot_v1.py` in this directory.

The [native sustained-flow preflight](../../../docs/notes/fluid-25d-native-sustained-flow-preflight-v1.md)
audits two unchanged Terrain Diffusion sites before adding a localized continuous
source. It separates D8 locators from raw cardinal routes, source-footprint
relief, and derived local depression storage. Neither site is selected for the
first continuous-stream pilot; the lowland starts inside a large depression and
the mountain reference is narrow and steep at native 30 m spacing. Reproduce the
offline audit with `native_sustained_flow_preflight_v1.py` in this directory.
Its static fill volumes are not required transient water mass. No new Fluid
protocol, source tuning, default, or promoted scene follows from this audit.

The follow-up [native flow-site survey and pilot](../../../docs/notes/fluid-25d-native-flow-site-survey-v1.md)
screens 240 reaches in the same 12 cached payloads, then tests two human-reviewed
sites with a shared 30/60 m³/s continuous source. Its separate opt-in
`natural-flow-study` scenario requires a hash-pinned recipe, exact native 30 m
crop, explicit positive `--fluid25d-natural-flow-source-m3-per-s`, and
`finite-volume`. Terrain starts dry and is never modified. Five face-connected
source cells receive equal depth-rate input; there is no prescribed source
momentum, rain, interior sink, or finite source duration. Every perimeter is
outward-only. The amber expected-exit window measures discharge; it does not
force drainage there. Existing neutral terrain protocols and defaults remain
unchanged. All four pilot cases established continuous flow; site A / 30 is
retained as the visual study, not a promoted product fixture or a natural-river
prediction. The user subsequently rejected its low-relief presentation as too
flat/subtle; retain that feedback separately from its numerical success. The
note contains matched motion/dye evidence and strict-parity
limits.

The follow-up [macro hillside-flow study](../../../docs/notes/fluid-25d-hillside-flow-study-v1.md)
uses a separate opt-in `hillside-flow-study` mode on a 256x256 native-30m
mountain domain. It has continuous upland supply, a dry start, unchanged
terrain, and no prescribed outlet or interior drain. Only the green source
marker is shown. Elevation-band observations measure actual downhill wetting,
not arrival at a chosen exit. The source-only recipe uses the existing
`--fluid25d-natural-flow-recipe` and source-rate flags with a distinct strict
schema; an old expected-outlet recipe cannot silently become a hillside case.
Whole-domain and close-front captures, short 512x512 cost evidence and limits
are in the note. Defaults and neutral rain/sheet protocols remain unchanged.

For the hillside window, `--fluid25d-hillside-source-context` selects a closer
render-only source camera. Normal playback is continuous, with the source
always supplying water; `Space` pauses/resumes it. The optional
`--fluid25d-hillside-inspection-advance-seconds 1800`
executes every ordinary solver step from the dry start and then pauses at
30 simulated minutes. The panel retains ten-minute compute-and-pause under
Inspection tools, Cancel and physical-time feedback. Its primary advance
action computes to 55 simulated minutes and resumes continuous playback.
This is not a larger numerical dt, a
prefill, or a skipped water evolution, and startup advance is rejected in
headless mode. Reset restores dry start and cancels pending advance.

For repeatable **continuous** local review, run:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py
```

This uses the same dry-start 256x256 native-30m terrain and 100 m³/s supply,
starts at 8x windowed playback with the closer source camera, and runs until
you close the window. By default it requests neither advance nor timed pause.
Use `--camera overview`, `--view water-isolation` / `flow-inspection`,
`--playback 1`, or `--print-command` for presentation-only alternatives.
The panel displays physical time, supply rate, and continuous/paused status.
The original launcher remains water-only by default. `--domain 512 --dye
--view transport-inspection` explicitly selects the wider crop and a conserved
60-62-minute source pulse; clear water supply remains continuous. See the
[V3 conservation/transport pass](../../../docs/notes/fluid-25d-hillside-conservation-transport-v3.md)
for its numerical and presentation gates.

The opt-in [motion/readability V4](../../../docs/notes/fluid-25d-hillside-motion-readability-v4.md)
adds source-released pale markers and short
trails, a fixed logarithmic hillside dye scale, and explicit source, branch,
and overview camera presets. Markers sample the accepted depth-averaged
velocity; they are presentation state, not simulated floating objects or
additional hydraulic forcing. They disappear at dry support or crop exits,
freeze on a rejected hydraulic update, and reset with the dry start. The
fixed dye legend is 0.1%, 1%, 10%, 100% of injected concentration, with a
0.01% clear-water floor. It is concentration, not speed or depth.

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py \
  --domain 512 --markers --dye --view transport-inspection --developed
```

`--developed` executes ordinary fixed steps to 55 physical minutes with
visible progress, then continues at 8x. Green cubes mark the upland source tiles;
there is no selected drain. `--camera branch` frames the downstream split.
Marker-free launch defaults retain the established camera. These controls
do not change terrain, source rate, solver dt, or the `virtual-pipes` default.
`run_hillside_motion_v4.py --phase smoke|profiles|captures|review --out
<directory>` separates short media QA, exact two-hour V3 numerical comparisons,
matched videos, and receipt verification. Each phase refuses to overwrite
existing evidence. Full captures require passing profiles from the same app
and shader identity; retained V3 strict results are a labelled replay bridge,
not a claim that strict CPU comparisons were rerun under the V4 app.
An explicit `--phase profiles --revalidate-profiles` validates complete retained
executions into a separate receipt, preserving the earlier rejected report and
all artifacts. It refuses changed inputs, unsuccessful children, or incomplete
receipts; it does not rerun the simulation or alter numerical tolerances.

The V5 readability/response options stay on this same immutable hillside;
they do not audition another river site or develop the authored-river demo:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py \
  --domain 512 --markers --local-markers --depth-cues \
  --view water-isolation --camera travel --developed
```

Local dots show movement **here**, not parcels that travelled from the source.
The bounded 512-marker pool seeds areas at least 1 cm deep on fixed simulation steps;
active paths are never periodically teleported. The original source-release
mode remains available by omitting `--local-markers`. The opt-in fixed depth
palette spans 1 cm / 10 cm / 1 m / 10 m; this is depth, not speed. Its modest
shoreline coverage smoothing does not reconstruct the native 30 m geometry.
`--camera collection` frames the observed western collection pocket on the
512 reference crop; `travel` frames its intervening downhill region. These two
presets require `--domain 512`, because the smaller crop omits that pocket. The old
source/branch/overview cameras and launcher defaults remain available.

Add `--response` for a continuous supply experiment: 100 m³/s for 0–60 physical
minutes, 150 for 60–90, 50 for 90–120, then 100 indefinitely. The first two hours
inject the same 720,000 m³ as the constant reference. These are actual hydraulic
changes, unlike marker, camera, or palette switches. Leave dye off for the
matched forcing comparison. GUI Low/Base/High buttons queue 50/100/150 m³/s for
the next fixed step; a manual selection overrides the script until Reset.
Pause preserves that queue and clock; Reset clears both manual and scripted
progress. Playback speed changes wall-time pacing, not input strength or dt.
The GUI continues until closed; finite headless captures are evidence windows,
not a timed simulation stop. `run_hillside_readability_v5.py` separates profile,
captures, and review phases under a fresh output directory. Human animation and
live GUI acceptance remain separate from its automated gates. The optional
`run_hillside_pacing_v5.py --out <fresh-output-root>` records serialized 900-frame
marker-off/on Xvfb playback runs with an independently checked supply ledger;
this is offscreen-windowed pacing evidence, not an isolated overhead benchmark.
The Diagnostics depth map retains its legacy linear palette (brighter cyan is
deeper, saturated at about 8.3 cm); the 3D log-depth legend does not apply there.
Use a Python environment with Pillow for the V5 capture/review phases:

```bash
rtk proxy uv run --python 3.12 --with pillow==12.3.0 python \
  projects/fluid/fluid_25d/run_hillside_readability_v5.py \
  --phase captures --out <output-root>
```

`--profile-workers 3` optionally overlaps the three independent
correctness jobs; those child wall times are not performance measurements.

The local V5 checkpoint is retained under
`outputs/fluid/hillside-readability-v5-20261001-o0PsLm/`; start with
`READING-GUIDE.txt` and `final-local-closure.json`. All three two-hour profiles
pass their automated gates: constant water matches V3, constant dye matches
V4, and the response matches the reference until its first supply change.
At 120 physical minutes, the response's observed collection depth is 7.136 m
versus 6.580 m, with equal 720,000 m³ input and no boundary export in either run.
The response is delayed downstream: at 75 minutes, source-region storage and
mean active speed already differ, but the observed collection depth and
material-front distance still match. Final stills alone conceal that delay.
Local-marker coverage in narrow downstream branches and native-grid geometry
remain readability limits; human animation and live GUI acceptance are pending.
This checkpoint does not establish calibrated hydrology or change solver defaults.

### Continuous mountain rain study

`hillside-rain-study` is a separate opt-in forcing experiment on the same
unchanged 512x512 native-30m Terrain Diffusion mountain crop. It starts dry,
adds uniform surface rainfall continuously, and has no point source, interior
sink, or chosen outlet. Every outward crop face permits export to a dry
exterior at that face's own bed elevation. Crop edges are not watershed bounds.
The virtual-pipes default and previous point-source and timed rain-pulse modes
are unchanged.

The two predefined cases are `equal-input` (1.52587890625 mm/h, 100 m³/s over
235.9296 km²) and `main` (12 mm/h, 786.432 m³/s). At two physical hours these
add 720,000 and 5,662,310.4 m³ respectively. All rainfall becomes surface water:
there is no infiltration, evaporation, soil, terrain erosion, or hydrological
calibration. The main case's direct rainfall depth is 24 mm after two hours;
merely seeing blue across the map is therefore not evidence of stream formation.

For indefinite live playback:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_demo.py
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_demo.py \
  --case equal-input --camera collection --markers --developed
```

`--developed` computes the first physical hour using every unchanged fixed
step, then continues at 8x requested playback. It does not prefill the terrain
or stop the live simulation. The UI shows physical time, applied mm/h and total
m³/s, accumulated rain, queued rain On/Off, and continuous/paused/advance state.
Rain changes apply at the next fixed step; pausing preserves a queued change.
Reset clears the water and rain clock and restores rain On. It is not a forced
pause: when running, normal fixed-step playback continues immediately after
the reset. Pause first to inspect the dry time-zero state. Camera presets are
absolute terrain locations (`overview`, `travel`, `collection`), not source
locations. There are no source/drain cubes. Optional pale dots are local motion
indicators, not rainfall drops or water parcels released from an endpoint.

The bounded study runner is independent of the indefinite launcher:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_v1.py \
  --phase smoke --out <fresh-output-root>
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_v1.py \
  --phase compatibility --out <output-root>
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_v1.py \
  --phase profiles --out <output-root>
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_rain_pacing_v1.py \
  --out <output-root>
rtk proxy uv run --python 3.12 --with pillow==12.3.0 python \
  projects/fluid/fluid_25d/run_hillside_rain_v1.py \
  --phase captures --out <output-root>
rtk proxy uv run --python 3.12 --with pillow==12.3.0 python \
  projects/fluid/fluid_25d/run_hillside_rain_v1.py \
  --phase review --out <output-root>
```

Profiles check every fixed-step rain integral, solver status, and the unchanged
water-conservation bound. Observations distinguish direct rainfall from water
gathered laterally. The two independent correctness profiles may overlap;
their timings are not an isolated performance measurement. Pacing runs later,
serially, without those diagnostic readbacks. Three predefined 21x21 regions
report storage minus their
own direct rain (a signed net storage measure, not gross inflow). Interior
D4-connected moving cells at least 1 cm deeper than cumulative direct rain
describe convergence corridors; span is the component's maximum X/Z extent,
not a parcel's travel distance. The screening gate requires at least 300 m
of corridor support for 900 consecutive physical seconds and positive excess
storage in a fixed region. This does not prove that one identical channel
persisted throughout, nor does it establish visual acceptance.

If the healthy two-hour main case lacks that support, the predefined extension
is an uninterrupted four-hour replay from dry start. Its entire two-hour
non-timing prefix must match exactly; it is not GPU checkpoint restoration.
Captures include matched two-hour rate comparisons, three labelled main-case
videos and exact-time stills. Video physical clocks are post-step: frame zero
is 2 s. Offscreen marker-off/on pacing excludes full-field diagnostics and is
not an isolated marker-overhead benchmark or desktop acceptance. Receipts bind
commands, terrain, app, shaders, profiles, media and logs; old evidence is never
overwritten. Human animation/live GUI acceptance remains a separate review.

The local V1 checkpoint is
`outputs/fluid/hillside-rain-v1-20261001-EQ9DU6/`. Both two-hour rates passed
the unchanged numerical gate and the convergence/collection screen, so the
conditional four-hour extension was not needed. At two hours, the main case
had a 5,910 m largest connected moving-water corridor span and about 54,004 m³
of net lateral storage in the collection footprint, whose maximum depth was
5.98 m. The equal-input case's final corridor span was 1,950 m and its collection
excess about 1,210 m³. These are observations, not calibrated river forecasts.
The main water-ledger residual stayed below 0.325 m³ over roughly 5.66 million
m³ supplied. Short serialized windowed runs achieved 7.91x/7.97x requested-8x
playback without backlog drops, but are not a desktop performance guarantee.
138 non-windowed native tests and 208 Python tests passed. Sampled captures
show terrain-shaped branching runoff and growing collection; the first ten
minutes remain visually quiet and native 30 m wet-edge steps are exposed in
oblique views. Sparse temporal samples do not establish flicker-free animation.
Start with the labelled valley video and `READING-GUIDE.txt`; the sealed local
closure leaves human animation/live GUI acceptance explicitly pending.

The [Rain V2 camera/transport diagnosis](../../../docs/notes/fluid-25d-hillside-rain-v2-transport-diagnosis.md)
fixes the far-plane bound for the full legal orbit range and adds an independent
uniform-sheet incline benchmark. It demonstrates substantial thin-water speed
underprediction from the existing first-order hydrostatic reconstruction,
despite safe CFL, depth preservation and CPU/GPU agreement. It does **not**
change solver defaults or promote a numerical correction. The predefined
48 mm/h mountain comparison and new live basin observations/cameras are
deferred behind that failed accuracy gate. Analysis-only depression masks on
the current immutable crop are not simulated lakes. See the note and the
sealed V2 evidence root for measurements, reproduction and acceptance limits.

The [Rain V3 CPU correction study](../../../docs/notes/fluid-25d-hillside-rain-v3-cpu-transport-study.md)
tests two isolated double-state prototypes without linking them into the app.
The slope-aware candidate passes the straight-incline and continuous-rain
references, but both candidates fail curved-flow accuracy; the fallback also
moves a partially wet resting lake. Neither is promoted. Production CPU/GPU
solvers, defaults, immutable terrain and forcing remain unchanged. Actual
applied face-transfer evidence and the negative verdict are retained under
`./outputs`; stronger mountain rainfall and GPU integration remain deferred.

The [sustained hillside V2 pass](../../../docs/notes/fluid-25d-hillside-sustained-flow-v2.md)
keeps the accepted terrain and forcing fixed over two physical hours. Its new
`run_hillside_sustained_flow_v2.py` separates hydraulic profiles, matched
captures, and a stop-on-first-failure strict oracle ladder. It records active
versus slow material-water volume, shared-face source connectivity, and crop
edge proximity as observations, without prescribing a route or changing the
terrain. A same-source 512x512 comparison is allowed only when the current
256x256 profile actually approaches an edge or exports water. App, compiled
shader, recipe, manifest, and elevation identities are checked before and
after each phase. See the note for measurements, evidence, and parity limits.
At the V2 checkpoint, strict 1/10/30-minute checks passed, but the extended water
ledger fails its unchanged tolerance after 68.5 minutes. Its later captures
are explicitly diagnostic, not a promoted two-hour pass; hillside dye also
was gated by the separately failing long tracer conservation check. Those
historical failures are retained; V3 diagnoses and corrects the demonstrated
rounding loss without changing forcing or tolerances.

`native_flow_site_survey_v1.py` reproduces the offline screening.
`run_natural_flow_pilot_v1.py --phase hydraulics --output-dir <new-directory>`
runs the frozen four-case matrix; after review, `--phase evidence --winner a:30`
with that same directory records matched captures and strict oracle checks.
Failed numerical or strict-oracle checks return a nonzero exit status while
retaining their reports; the known long-duration dye failure is not waived.
Output directories must be fresh for hydraulics, and captures are never
overwritten. Recipe gauges report signed depth–velocity discharge estimates,
not solver face flux. Separate expected-window/other-edge/corner ledgers retain
the existing boundary total. See the note for exact `rtk` commands and timing.

Analytic fixtures remain closed by default. `boundary-drain-fixture` exists
solely to validate the numerical outflow contract: an opened perimeter face
sees dry exterior at its own bed elevation, permits no external inflow, and
records its post-limiter discharged volume separately from source and sink
ledgers. It is not a product scenario or terrain audition policy.

In a window, the compact Fluid 2.5D panel selects the Catchment or Diagnostics
surface; while Catchment is selected, a second control chooses Composite, Water
Isolation, Flow Inspection, or Transport Inspection. The catchment choice is
retained when `A`
switches to Diagnostics and back. The panel also exposes Pause/Resume, Reset,
and the supported `0.125..8x` windowed playback speed. The legend reads the
terrain as the matte bed, bright cyan as shallower water, and darker blue as
deeper water. The moving highlight is a passive render-only marker advected by
velocity; it is not waves or a depth cue. Left-drag or scroll orbits/zooms the
catchment; `Space` pauses, `R` resets the deterministic scenario, and `D`
cycles diagnostic contents. UI controls only affect presentation/pacing; solver
fixed delta, headless timing, and numerical evidence remain unchanged.

In Flow Inspection, arrows stay anchored to a uniform grid: angle means local
flow direction, and length/brightness mean speed. They sample the velocity
field and are not moving water particles. The source-to-outlet scene retains
its green SOURCE and amber DRAIN cube patches in this view.

Transport Inspection is separate from Flow Inspection: it hides the quiver and
render-only directional cue so one visual language remains. Green cubes mark
input, base blue shows water depth, and magenta/violet shows conservative dyed
water. The `source-outlet-demo` uses amber cubes for explicit removal;
the sustained-headwaters control uses two green input patches and an open-edge
outlet without sink cubes. The natural-flow study's expected exit remains a
diagnostic observation window, with all perimeter edges open independently. The pulse is
available only with finite-volume, both dye timing options, and
`source-outlet-demo`, `sustained-headwaters-demo`, `natural-flow-study`, or
`hillside-flow-study`. The fixed hydraulic forcing is unchanged by the dye
schedule. See the demo and hillside sections above for
their captures and frame semantics; the earlier V1 study remains in
[`docs/notes/fluid-25d-transport-readability-v1.md`](../../../docs/notes/fluid-25d-transport-readability-v1.md)
as historical evidence.

## Source layout

- `projects/fluid/fluid_25d`: project-facing CMake and design authority;
- `projects/fluid/fluid_25d/run_terrain_water_audition.sh`: matched evidence
  runner for the immutable virtual-pipes V1 baseline;
- `projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh`: finite-volume
  physical-horizon audition runner with solver-qualified ignored outputs;
- `projects/fluid/fluid_25d/run_mountain_source_outlet_demo.sh`: finite-volume
  review runner for the pinned immutable mountain source/outlet scene;
- `projects/fluid/fluid_25d/run_sustained_headwaters_demo.sh`: dry-start
  two-source control and dye-motion evidence runner;
- `projects/fluid/fluid_25d/run_rain_dynamics_study_v1.sh`: matched neutral and
  visibility-stress finite-volume rain-dynamics study runner;
- `projects/fluid/fluid_25d/run_transport_readability_v1.sh`: deterministic
  source/outlet dye-pulse video, profile, checkpoint, and acceptance runner;
- `projects/fluid/sim/fluid_25d`: project-local config, scenarios, CPU oracle,
  and focused tests.

The solver is intentionally not coupled to `projects/terrain`, `projects/ocean`,
`TerrainOceanFieldView`, paused hydrology work, or the shared rendering
foundation. Those products may become future consumers only after a concrete
contract is demonstrated.

## Next implementation gate

Review the integrated continuous mountain-rain service in the GUI before any
presentation/product promotion. Keep the built-in solver as the simpler option,
the stock bridge/recordings as references, and terrain inputs immutable. No
solver port, new terrain search or numerical replacement follows implicitly
from this integration. Shared helpers still require an independent consumer or
a measured bottleneck.
