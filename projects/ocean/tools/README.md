# ocean capture and profiling tools

Reusable project-owned helpers, kept out of the application source root.
Start with the [project guide](../README.md). Scripts require an explicitly built
application and their declared optional assets; nothing here runs at startup.
Some capture helpers open a native window. Choose headless operation explicitly
for automation; capture/encoding timings are not native presented FPS.

- [capture_ocean_review.sh](capture_ocean_review.sh)
- [profile_cloud_reflections.sh](profile_cloud_reflections.sh)
- [profile_ocean_baseline.sh](profile_ocean_baseline.sh)
- [profile_scene_ocean_ablation.sh](profile_scene_ocean_ablation.sh)
