# Planet study capture tools

Start with the [paused study guide](../README.md#explicit-build-and-use).
The helper requires an explicitly built `dev-planet-studies` executable and
fails before creating outputs if it is missing. Set `APP` (or `BUILD_DIR`) to
choose another explicit study build. It captures headlessly and never builds
the application or deletes the output directory. Use a fresh directory to
preserve earlier evidence; capture timings are not native presented FPS.

- [capture_orbital_review.sh](capture_orbital_review.sh)
