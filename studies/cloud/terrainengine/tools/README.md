# Cloud reference capture tools

Start with the [opt-in study setup](../README.md#explicit-build-and-use), not the
normal project build. These scripts use the `dev-cloud-studies` binary by default;
set `APP` to use another explicit build. All three use Vulkan headless capture
and do not open a window. Nothing runs at project startup.

- [capture_lighting_compare.sh](capture_lighting_compare.sh)
- [capture_review.sh](capture_review.sh)
- [capture_sampling_compare.sh](capture_sampling_compare.sh)

Atmosphere owns the optional cross-renderer helpers:
[parity](../../../../projects/atmosphere/tools/capture_cloud_ref_parity.sh),
[sampling](../../../../projects/atmosphere/tools/capture_cloud_sampling_perf.sh),
and [native-window profiling](../../../../projects/atmosphere/tools/profile_cloud_vs_ref.sh).
Those require both builds; native-window profiling is manual-only, not headless
throughput evidence. Capture/encoding timings are not native presented FPS.
