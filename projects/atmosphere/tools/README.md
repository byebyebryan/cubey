# atmosphere capture and profiling tools

Reusable project-owned helpers, kept out of the application source root.
Start with the [project guide](../README.md). Scripts require an explicitly built
application and their declared optional assets; nothing here runs at startup.
Some capture helpers open a native window. Choose headless operation explicitly
for automation; capture/encoding timings are not native presented FPS.

`capture_cloud_ref_parity.sh`, `capture_cloud_sampling_perf.sh`, and
`profile_cloud_vs_ref.sh` additionally require the
[opt-in cloud reference study](../../../studies/cloud/terrainengine/README.md#explicit-build-and-use).
Their reference default is the `dev-cloud-studies` binary, not a normal project
build; set `CLOUD_REF_APP` to choose another explicit build. The profiling helper
opens native windows and is for manual runs only; both capture helpers are headless.

`capture_star_field_review.sh` reviews Atmosphere only. The obsolete Planet
surface/orbit rows were removed when Planet became a paused study; orbital
comparison captures belong to the [study tools](../../../studies/planet/tools/README.md).

- [capture_atmosphere_background_horizon.sh](capture_atmosphere_background_horizon.sh)
- [capture_cloud_edge_resolve.sh](capture_cloud_edge_resolve.sh)
- [capture_cloud_farfield_handoff.sh](capture_cloud_farfield_handoff.sh)
- [capture_cloud_lighting_regimes.sh](capture_cloud_lighting_regimes.sh)
- [capture_cloud_ref_parity.sh](capture_cloud_ref_parity.sh)
- [capture_cloud_review.sh](capture_cloud_review.sh)
- [capture_cloud_sampling_perf.sh](capture_cloud_sampling_perf.sh)
- [capture_cloud_surface_horizon_regimes.sh](capture_cloud_surface_horizon_regimes.sh)
- [capture_cloud_terrainengine_study.sh](capture_cloud_terrainengine_study.sh)
- [capture_milky_way_layers.sh](capture_milky_way_layers.sh)
- [capture_moon_review.sh](capture_moon_review.sh)
- [capture_star_field_review.sh](capture_star_field_review.sh)
- [profile_cloud_vs_ref.sh](profile_cloud_vs_ref.sh)
- [profile_star_field.sh](profile_star_field.sh)
