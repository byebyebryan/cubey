# Shared surface environment V1

2026-10-14 · optional integration, hardware headless Vulkan only.

[Matched views and clips](http://starship:8787/reports/a9e51f964348adf9f3963655b4dfe06061b3fc0c88c4717e436d537c9bc76bb0/)
· [Cubey report catalogue](http://starship:8787/reports/#project=cubey)

## Contract

Fluid remains a detail-patch-only native viewer. Shared Terrain retains its saved
water and backdrop geometry/LOD. `TerrainSurfaceEnvironment` owns one existing
`AtmosphereEnvironmentRuntime` and its `CloudEnvironmentRuntime`; it accepts no
hydraulic state or solver clock. Fixed daylight/clear remains the startup default.
There is no solver/CUDA/interop work or new meteorology in this pass.

Solar time advances from wall delta, even when water is paused or the recording
ends. Preset selection holds the clock; a separate cycle control resumes it.
Cloud wind uses independent elapsed wall time. Managed Fluid replacements move
the same controller only after active GPU submissions retire; asynchronous
replacement uploads never mutate active weather resources.

Six-face capture/prefilter completion and previous/current reflection blending
reuse the shared 4 Hz probe products. Diffuse/direct coefficients use the existing
SH/atmospheric-transmittance math, in fence-retired frame-slot buffers. Water,
terrain, whitewater and rain share the resulting illumination. Dynamic night
removes the old daytime floor in water-body scatter and rain brightness; fixed
reference shading is untouched.

The shared sky draws inside the opaque HDR scope. Cloud composition preserves
opaque terrain and precedes water/refraction; no nested rendering scope or
cloud-consumable water depth is introduced. Approximate cloud direct attenuation
is independently switchable; whitewater explicitly declares the same shadow
texture dependency. Structural cloud-quality/history edits retire the GPU queue
before replacing target resources. Normal frame updates do not wait there.

## Controls

- Launcher: `--environment fixed|shared`, `--environment-time-hours 0..24`,
  `--environment-cycle-speed 0..1` hours per wall second.
- Clouds: `--clouds`, `--cloud-weather surface-volume|broken-cumulus|overcast-stratus`,
  `--cloud-quality quarter|half|full`, `--cloud-shadow-strength 0..1`.
- Native Fluid: `--fluid25d-environment shared` plus existing atmosphere/cloud
  schema flags; independent background/reflection negative flags and shadow strength.
- Terrain saved water: `--terrain-water-environment shared`, existing environment
  flags and `--terrain-water-cloud-shadow-strength 0..1`.
- GUI: compact Environment controls plus the existing advanced shared controls.
  GUI acceptance is deferred; all automated runs used offscreen Vulkan.

Atmosphere boolean CLI values use `--set atmosphere.time_paused=false` when an
explicit false value is needed; `--pause-time=false` is not supported. “Daylight”
restores the manual reference sun angle, not solar noon. Noon/sunset/night matched
captures use day 80, latitude 45 degrees; receipts retain exact recipes.

## Current evidence

Evidence root: `outputs/fluid/environment-v1-20261013-mmKabf`.

- Eight fixed-default frames are pixel-exact against pre-change captures.
- Twenty matched time/weather/component frames, two lower-oblique views, four
  Terrain saved-water views and two decoded clips retain exact GPU bed/depth/velocity
  imports. 443 input files and both app/shader identities remain frozen.
- A 32-second full solar cycle holds native water fields. A six-second wind/flow-cue
  clip also holds fields. These clips are presentation motion, not a running solver.
- Nine managed initial/replacement scenes start dry, retain native GPU imports,
  preserve a monotonic weather clock and close all owned process groups without
  escalation. The multi-grid lifecycle timings are not steady-state performance.
- 55 named regression groups pass without skips, including existing atmosphere,
  Ocean cached-cloud and Water3D cloud consumers, and terrain/water contracts.

1080p renderer-only, two reversed-order 36-second trials per recipe, 120 warmup
frames, every frame sampled; shared runs cross a full cycle:

| Grid / treatment | GPU p95, ms |
| --- | --- |
| 1k reference | 3.60–4.44 |
| 1k cycling clear | 4.57 |
| 1k cycling weather, half clouds | 5.43–5.48 |
| 1k cycling weather, full clouds | 5.53–5.57 |
| 2k reference | 9.04–9.15 |
| 2k cycling weather, half clouds | 11.03–11.11 |

The 1k half-cloud generation-change frames reach 6.15 ms p95 in the first
renderer-only repeat, versus 4.18 ms between captures. Whole-run p99/max and
generation-change cohorts are retained; averages alone hide periodic costs.

With the actual CUDA worker running, 90-second trials, 20 seconds of running
warmup excluded, every frame sampled, requested 60x solver pace and 60 Hz render cap:

| Mode | GPU p95 ms | CPU service p95 ms | Advancing field age p95 s | Achieved pace |
| --- | --- | --- | --- | --- |
| Fixed daylight, two controls | 5.32 | 2.72–2.77 | 0.144–0.146 | 59.99–60.03x |
| Cycling weather, half clouds, two repeats | 8.36–8.42 | 2.76–2.79 | 0.141 | 59.90–59.99x |

Combined weather **fails the proposed 8 ms GPU p95 gate**. CPU service (<4 ms),
advancing field age (<1 s) and solver pace (>=54x) pass. Its p99 is 9.48–9.86 ms,
maximum 11.41 ms; that is below a 16.67 ms rendering-work budget but is not native
presentation/scanout qualification or a promoted default.

The five-minute weather soak closes cleanly, keeps 59.99x pace, and advances the
environment to 299.85 s. Full-duration status supervision retains publication age
p95 0.112 s. The existing 12,000-frame profile cap limits per-frame timing to the
first roughly 203 s (181 s after running warmup), not the complete soak. Retained
GPU p95 is 8.10 ms, CPU service 2.76 ms, advancing field age 0.142 s. Exact final
native GPU imports pass after the full run.

## Limits and verdict

Day/night is useful and integrated; water no longer keeps daytime illumination at
night. Night is intentionally dark, and an accelerated cycle exposes the existing
primary sun/moon handover. These are not polished astronomical transitions.

Clouds are the existing surface V1, not a qualified aerial/cloud-top renderer.
Elevated patch views can show little cloud or an unconvincing cloud horizon; the
lower-oblique review exposes more of the layer. The backdrop uses the existing
sky-only horizon policy, while IBL keeps the real ground policy. This is a surface
backdrop approximation, not a full physically altitude-consistent aerial atmosphere.

Cloud shadows use one bounded receiver plane, soft footprint fade, direct-light
attenuation only. They are not relief-aware cascades or qualified full-world
shadows. Ambient diffuse remains clear-sky SH, not a physically complete overcast
GI solution. No planar Ocean reflection is added to varying stream elevations.

Keep cloud background, cached reflections and projected shadows independently
optional. Preserve fixed daylight/clear startup. The combined live weather cost is
a measured limitation, not a passing strict-budget claim. No push, default promotion,
native GUI or human visual acceptance is part of this loop.

## Reproduce safely

`review_environment_v1.py OUTPUT --phase freeze|frames|terrain|motion|performance`
uses existing immutable cases and hardware headless Vulkan. Use a fresh output;
it refuses to overwrite existing evidence. `review_patch_only_v1.py` supplies the
eight fixed controls. `check_environment_regressions.py OUTPUT` runs the named
serial, headless-only CTest set. `validate_terrain_demo_v1.py` supplies managed
CUDA/Vulkan lifecycle and performance checks; `--profile-sample-interval 1`
captures probe bursts. It installs nothing and supervises only owned processes.

After all evidence exists, `report_environment_v1.py OUTPUT` checks the frozen
runtime/inputs, complete matrix, native audits, named regressions and live cleanup
before making a dedicated export. Publish that directory with `codex-plus-report`
under project `cubey`; leave the shared server running. `check_environment_report.mjs`
uses its own headless Chrome profile, checking selections, decoded videos, links,
mobile width and the no-script fallback. It never attaches to a user's browser.

Existing cloud-foundation, dynamic-probe coherence and Ocean cloud-lighting notes
remain the underlying contracts. No new cloud algorithms or weather/hydrology
coupling was invented. Failed harness attempts are retained separately in the
evidence root; corrected observation/clip recipes used the same frozen runtime
and immutable inputs, without relaxing native field checks.
