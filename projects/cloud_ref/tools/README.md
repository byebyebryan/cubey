# cloud_ref capture and profiling tools

Reusable project-owned helpers, kept out of the application source root.
Start with the [project guide](../README.md). Scripts require an explicitly built
application and their declared optional assets; nothing here runs at startup.
Some capture helpers open a native window. Choose headless operation explicitly
for automation; capture/encoding timings are not native presented FPS.

- [capture_lighting_compare.sh](capture_lighting_compare.sh)
- [capture_review.sh](capture_review.sh)
- [capture_sampling_compare.sh](capture_sampling_compare.sh)
