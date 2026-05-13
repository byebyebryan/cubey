# glTF Assets And PBR

Cubey now has a narrow static glTF 2.0 path for imported assets. The goal is
not a complete DCC/runtime pipeline yet; it is a foundation contract for common
runtime asset data, PBR material inputs, texture upload, and a viewer project
that can keep pressure on the renderer.

## Precedent

- glTF 2.0 is the asset interchange target. The first importer follows core
  mesh, node, image, sampler, and metallic-roughness material terminology
  rather than inventing Cubey-specific names.
- Khronos glTF Sample Assets are the default external reference set. CMake can
  point at an existing checkout with `CUBEY_GLTF_SAMPLE_ASSETS_DIR` or fetch it
  with `CUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON`.
- Filament remains the practical reference for keeping asset loading,
  renderable resources, material instances, views, and renderer policy separate.

## Current Scope

`cubey::asset` owns CPU-side loaded asset data:

- static glTF/glb parsing through `cgltf`;
- external buffers, data URIs, image buffer views, PNG/JPEG decode through
  `stb_image`;
- mesh primitives with position, normal, tangent, and UV0;
- metallic-roughness PBR material factors and texture references;
- sampler filtering/wrapping metadata;
- scene roots and node hierarchy with decomposed TRS transforms.

Unsupported features fail early instead of being silently ignored:
animations, skins, and morph targets are rejected by the loader. Extensions,
multiple UV sets, vertex colors, sparse accessors, material variants,
transmission, clearcoat, IBL, animation, skinning, and streaming remain future
slices.

`cubey::render` owns the reusable GPU-facing pieces:

- `PbrVertex`, `PbrSceneUniforms`, `PbrMaterialFactors`, and
  `PbrPushConstants` define the current shader contract;
- `pbr_forward_pass_info()` declares the scene set, material texture set, push
  constants, and depth state for the first PBR forward pass;
- `create_uploaded_texture_2d()` handles setup-time RGBA8 texture upload into a
  sampled image;
- `ShadowMapPass3D` owns a sampled depth texture plus depth-only pipeline for
  directional shadow passes.

`projects/gltf_viewer` is the integration project. It loads an input asset from
`--input`, falls back to the Khronos DamagedHelmet sample when the sample-assets
directory is configured, and otherwise renders a generated PBR cube. It creates
scene renderables from static glTF nodes, builds a shadow view and camera view,
records the shadow and PBR scene passes through the render graph, and keeps
material textures/descriptors project-owned.

## Boundaries

The importer produces CPU data only. It does not create entities, renderable
handles, textures, descriptors, pipelines, or scenes.

The render layer exposes contracts and helpers, not a full material system.
Texture lifetime, descriptor writes, material sorting policy, alpha policy, and
shader selection still belong to the project or future renderer layer.

The viewer is allowed to duplicate some bridge code while the asset/resource
contract settles. Once another project needs imported assets, the next
candidate extraction is a small asset-to-render-resource bridge that maps glTF
meshes/materials into registry handles and app-owned resource tables.

## Next Slices

- Add a headless viewer/capture path once renderable asset output needs visual
  regression checks.
- Split material texture upload by declared color space and sampler axes more
  completely.
- Add image/texture deduplication so shared glTF images are not uploaded once
  per material slot.
- Add IBL/environment inputs before judging PBR quality.
- Add alpha-mask/alpha-blend pass policy instead of treating all PBR draws as
  the same opaque forward pass.
- Add validation assets from Khronos Sample Assets as optional tests when the
  CI/dev environment can afford the download.
