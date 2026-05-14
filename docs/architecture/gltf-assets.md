# glTF Assets And PBR

Cubey has a narrow static glTF 2.0 path for imported assets. The goal is not a
complete DCC/runtime pipeline yet; it is a foundation contract for common
runtime asset data, PBR material inputs, texture upload, scene import, and a
viewer project that keeps pressure on the renderer.

## Precedent

- glTF 2.0 is the asset interchange target. Cubey follows core mesh, node,
  image, sampler, and metallic-roughness material terminology rather than
  inventing project-specific names.
- Khronos glTF Sample Assets are the default external reference set. CMake can
  point at an existing checkout with `CUBEY_GLTF_SAMPLE_ASSETS_DIR` or fetch it
  with `CUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON`.
- Filament remains the practical reference for keeping CPU asset data, engine
  scene import, renderable resources, material instances, views, and renderer
  policy separate.

## Current Scope

`cubey::asset` owns CPU-side loaded asset data:

- static glTF/glb parsing through `cgltf`;
- external buffers, data URIs, image buffer views, PNG/JPEG decode for glTF
  textures, and standalone Radiance HDR decode through `stb_image`;
- mesh primitives with position, normal, tangent, UV0, optional `JOINTS_0` /
  `WEIGHTS_0`, and optional morph target deltas;
- metallic-roughness PBR material factors and texture references;
- factor-only `KHR_materials_ior` and `KHR_materials_specular` controls;
- core glTF alpha modes: `OPAQUE`, `MASK`, and `BLEND`;
- sampler filtering and per-axis wrapping metadata;
- scene roots and node hierarchy with decomposed TRS transforms;
- core glTF animations, skins, node skin bindings, node/mesh morph weights, and
  inverse bind matrices.

Unsupported features fail early instead of being silently ignored:
additional skin influence sets, sparse accessors, unsupported morph target
attributes, and extension-only animation paths are rejected by the loader.
Other extensions, multiple UV sets, vertex colors, material variants,
transmission, clearcoat, glTF environment extensions, advanced animation
runtime features, and streaming remain future slices.

`cubey::engine` owns the current asset-to-scene bridge and renderer instance
service:

- `GltfSceneImportResources` stores app-owned mesh resources, material
  instances, material factors, uploaded material textures, and default PBR
  textures;
- `import_gltf_scene()` maps static glTF nodes into scene entities, 3D
  transforms, renderables, registry-issued mesh/material handles, imported
  bounds, and triangle counts;
- glTF alpha modes map into explicit render material alpha policy: `MASK`
  stays depth-writing and shadow-casting with alpha cutoff, while `BLEND`
  renders forward-only with premultiplied source-over alpha blending and no
  depth writes;
- texture upload is deduplicated per glTF texture plus color space, and
  sampler `wrapS` / `wrapT` are preserved through Vulkan sampler axes;
- `destroy_gltf_scene_import()` tears down imported resources without making
  the engine own Vulkan texture or mesh lifetime globally;
- `RendererService` owns renderer instance lifetime, and
  `ForwardPbrRenderer3D` owns the reusable shadow map, skybox, forward PBR
  pipelines, HDR scene-color target, post pipeline, scene/skybox/post material
  descriptors, depth attachment, and render graph recording for a 3D PBR view.
  Per-frame rendering enters through `ForwardPbrRenderer3DRenderRequest`, which
  groups target state, view plans, material/resource tables, and
  display/environment settings.

Implementation ownership follows the same boundary: `gltf_asset.cpp` owns
`cgltf` parsing and CPU asset construction, while `gltf_asset_io.cpp` owns URI,
data-URI, and image decode helpers. `gltf_scene_importer.cpp` owns entity,
transform, mesh, and scene import, while `gltf_scene_importer_materials.cpp`
owns default textures, texture upload, and material instance creation.

`cubey::render` owns the reusable GPU-facing pieces:

- `PbrVertex`, `PbrSceneUniforms`, `PbrPostUniforms`, `PbrMaterialFactors`,
  `PbrMaterialUniforms`, and `PbrPushConstants` define the current shader
  contract;
- `pbr_forward_pass_info()` declares the scene uniform/shadow/IBL set, material
  texture plus uniform set, model-only push constants, and opaque/alpha forward
  pass state;
- `ForwardPbrRenderer3D` records opaque/masked PBR packets before blended
  packets; blended packets are sorted back-to-front by view-space depth for
  basic source-over transparency;
- `pbr_post_pass_info()` declares the fullscreen post set that samples linear
  HDR scene color and applies display transform before writing the final target;
- `create_uploaded_texture_2d()` and `create_uploaded_texture_cube()` handle
  setup-time sampled texture uploads for glTF textures and IBL cubemaps;
- generated and equirectangular HDR PBR environment helpers provide
  deterministic or asset-backed irradiance, GGX-prefiltered radiance, and DFG
  LUT resources for the current viewer checkpoint;
- `ShadowMapPass3D` owns a sampled depth texture plus depth-only pipeline for
  directional shadow passes.

`projects/gltf_viewer` is the integration project. It loads an input asset from
`--input`, falls back to the Khronos DamagedHelmet sample when the sample-assets
directory is configured, and otherwise renders a generated PBR cube. It can use
`--environment path/to/env.hdr` or the optional fetched Filament
`lightroom_14b.hdr` sample for HDR-backed IBL and skybox rendering; without
one, it falls back to the generated environment. It creates camera and light
entities around imported bounds, builds shadow and scene frame plans, and hands
those plans plus material/resource tables to an engine-owned
`ForwardPbrRenderer3D` through `ForwardPbrRenderer3DRenderRequest` for pass
recording. Its PBR shader writes linear HDR scene color and uses the shared
Cubey PBR helper include for
base-color-to-diffuse/F0 remapping, reflectance/specular factor controls,
correlated Smith direct visibility, DFG-based IBL energy compensation, and
indirect specular occlusion. Material texture and factor alpha remain
straight/unassociated inputs; blended fragments emit premultiplied RGB at
shader output, while opaque and kept masked fragments output alpha 1. Display
transform is applied by the shared post shader.

## Boundaries

The asset loader stays CPU-only. It does not create entities, renderable
handles, textures, descriptors, pipelines, or scenes.

The engine importer is the current bridge between asset data and runtime scene
resources. It creates scene/render handles and app-owned resource tables, but
it does not choose shaders, record passes, allocate pipelines, or define
renderer-wide material policy. `RendererService` owns renderer instance
lifetime only. `ForwardPbrRenderer3D` is a separate engine-layer renderer
implementation: it records one PBR view using caller-provided shader paths,
frame plans, material tables, environment resources, and render settings.

The render layer exposes contracts and helpers, not a full material system.
Texture lifetime, descriptor writes, shader selection, and environment
selection still belong to the project or future renderer layer. Transparency V1
supports glTF alpha mask and alpha blend, but not refraction, transmission,
transparent shadow opacity, weighted blended transparency, or order-independent
transparency. Specular textures, clearcoat, transmission, and other glTF
material extensions remain future slices.

## Next Slices

- Add validation assets from Khronos Sample Assets as optional tests when the
  CI/dev environment can afford the download.
- Add prefiltered KTX/KTX2 environment loading and offline filtering now that
  generated and direct-HDR setup-time IBL paths have proven the renderer-side
  cubemap contract.
- Add vertex colors, multiple UV sets, and glTF material extension slices as
  real sample assets require them.
