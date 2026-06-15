# Cloud

`cloud` is the production volumetric cloud project. It starts from the
texture-backed density path proven in `projects/cloud_ref`, while leaving
`cloud_ref`, `cloud_ref_2`, and `clouds_legacy` intact as reference projects.

Current V1 scope:

- generated 128^3 Perlin-Worley base noise;
- generated 32^3 Worley erosion/detail noise;
- generated 1024^2 weather coverage/type map;
- spherical shell raymarch with height gradients, detail erosion, Beer
  transmittance, powder response, and a short light march;
- separate cloud product and composite passes declared through
  `RenderGraphBuilder`;
- shared `clouds.*` `RunConfig` options and hand-authored ImGui controls;
- diagnostics for weather, base/detail density, density, transmittance,
  lighting, shadow, cloud alpha, distance, steps, and background.

The first target is cloud shape: raw density and final captures should show
coherent cloud masses without relying on cache, temporal reconstruction, or
final blur. Validate against `projects/cloud_ref`; do not tune this project
toward `cloud_ref_2` visuals.

Useful runs:

```sh
./build/dev/projects/cloud/cloud
./build/dev/projects/cloud/cloud --cloud-camera-mode surface-up
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique
./build/dev/projects/cloud/cloud --cloud-weather-preset fair-weather
./build/dev/projects/cloud/cloud --cloud-weather-preset broken-cumulus
./build/dev/projects/cloud/cloud --cloud-weather-preset storm-cells
./build/dev/projects/cloud/cloud --debug-view raw-final
./build/dev/projects/cloud/cloud --debug-view weather
./build/dev/projects/cloud/cloud --debug-view base-density
./build/dev/projects/cloud/cloud --debug-view detail-density
./build/dev/projects/cloud/cloud --debug-view density
./build/dev/projects/cloud/cloud --debug-view transmittance
./build/dev/projects/cloud/cloud --debug-view lighting
./build/dev/projects/cloud/cloud --debug-view cloud-alpha
./build/dev/projects/cloud/cloud --debug-view distance
./build/dev/projects/cloud/cloud --debug-view steps
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode surface-up --output outputs/cloud-v1-surface-up.png
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode high-oblique --output outputs/cloud-v1-high-oblique.png
projects/cloud/capture_review.sh outputs/cloud-v1-review
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle diagnostic views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

Known deferrals:

- No cached octahedral hemisphere path yet. `cloud_ref_2` remains the cache
  architecture reference for a later pass.
- No temporal reconstruction yet. The raw direct signal must be credible first.
- No ocean, planet, terrain, or PBR integration yet. Future consumers should
  sample cloud outputs rather than owning cloud raymarch code.
- No promoted shared cloud renderer API yet. Textures, descriptors, materials,
  and synchronization remain project-owned in V1.

See
[`docs/architecture/cloud-rendering.md`](../../docs/architecture/cloud-rendering.md)
for the production cloud direction and promotion criteria.
