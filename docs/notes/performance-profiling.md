# Performance Profiling Notes

Use profile output when comparing renderer changes. GUI FPS is useful for
interactive feel, but it is an instantaneous host-frame readout and can disagree
with averaged profile output. The profiling path should record:

- `windowed_perf` from `--print-frame-stats` for whole-window throughput.
- CPU spans from `--profile-output`, especially `host.draw_frame`,
  `host.record_frame`, and `host.end_frame`.
- GPU timestamp spans for render-graph passes when the project owns a
  `GpuTimestampProfiler`.

For cloud integration work, start with:

```bash
projects/atmosphere/profile_cloud_vs_ref.sh
```

Useful overrides:

```bash
FRAMES=1200 WARMUP_FRAMES=180 REPEATS=5 WIDTH=1280 HEIGHT=720 \
  projects/atmosphere/profile_cloud_vs_ref.sh
```

The script compares:

- `cloud-ref`: standalone surface cloud reference.
- `atmo-clouds`: atmosphere sky, production clouds, and HDR post, with moon
  disabled for a cleaner baseline.
- `atmo-no-clouds`: atmosphere sky and post without clouds.

Read `summary.csv` first. If `windowed_ms` and `host_draw_frame_ms` disagree,
the bottleneck is likely outside recorded command generation. If `gpu_total_ms`
is much lower than host frame time, inspect CPU spans and presentation/swapchain
behavior before optimizing shaders. If one GPU pass dominates, optimize that
pass in isolation and rerun the same script before/after.

## Atmosphere Sky Pass Checkpoint

The current shared atmosphere sky pass is a brute-force physical scattering
reference path. It is useful as a quality baseline, but it is not the cheaper
runtime shape that most production sky systems use.

Current code path:

- The atmosphere shader uses 16 view samples and 8 light samples per view
  sample.
- Each sky pixel can therefore pay up to roughly 128 density and transmittance
  samples before any clouds are considered.
- Most atmosphere debug views select their output after the atmosphere
  integration has already run, so `rayleigh`, `mie`, `transmittance`,
  `sun-disk`, and `night-sky` are still representative of the full sky pass
  cost.
- Debug views that early-out before integration, such as `milky-way`, show the
  non-scattering baseline and are not representative of the production sky
  cost.

Measured baseline from `outputs/perf-atmo-cloud-vs-ref-20260706-125843`:

- `cloud-ref`: about 1.0 ms windowed frame time, with about 0.65 ms GPU time.
- `atmo-clouds`: about 1.8 ms windowed frame time, with about 1.13 ms GPU time.
- `atmo-no-clouds`: about 0.88 ms windowed frame time, with about 0.50 ms GPU
  time.

The gap between `cloud-ref` and `atmo-clouds` is mostly the fixed atmosphere sky
pass, not a slower cloud march. A sky-only debug sweep confirmed the physical
sky pass sits around 0.49 ms GPU time, while the early-out `milky-way` path is
around 0.03 ms.

This is the same problem space as the precomputed scattering work that keeps
coming up in sky-rendering references. The likely future direction is to keep
the current brute-force integrator as a reference/debug path, then add a runtime
path based on a transmittance LUT, sky-view LUT, and optionally an
aerial-perspective LUT. This note intentionally parks that optimization work for
later; it is not part of the current cloud integration cleanup.
