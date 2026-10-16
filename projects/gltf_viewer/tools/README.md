# gltf_viewer capture and profiling tools

Reusable project-owned helpers, kept out of the application source root.
Start with the [project guide](../README.md). Scripts require an explicitly built
application and their declared optional assets; nothing here runs at startup.
Some capture helpers open a native window. Choose headless operation explicitly
for automation; capture/encoding timings are not native presented FPS.

- [profile_gltf_loading.sh](profile_gltf_loading.sh)
- [profile_gltf_loading_tests.sh](profile_gltf_loading_tests.sh)
- [profile_gltf_windowed_upload_jitter.sh](profile_gltf_windowed_upload_jitter.sh)
