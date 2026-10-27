# gltf_viewer capture and profiling tools

Reusable project-owned helpers, kept out of the application source root.
Start with the [project guide](../README.md). Scripts require an explicitly built
application and their declared optional assets; nothing here runs at startup.
Some capture helpers open a native window. Choose headless operation explicitly
for automation; capture/encoding timings are not native presented FPS.

- [profile_gltf_loading.sh](profile_gltf_loading.sh)
- [profile_gltf_loading_tests.sh](profile_gltf_loading_tests.sh)
- [profile_gltf_windowed_upload_jitter.sh](profile_gltf_windowed_upload_jitter.sh)

The loading runner writes V2 manifests for current 48-metric output. Summary
replay keeps V1's historical 46-metric reports intact; schemas and incomplete
metric sets cannot be mixed. See the
[profile workflow and evidence boundary](../../../docs/notes/gltf-loading-profile.md#repeatable-workflow).
