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
- external buffers, data URIs, image buffer views, PNG/JPEG decode through
  `stb_image`;
- mesh primitives with position, normal, tangent, and UV0;
- metallic-roughness PBR material factors and texture references;
- sampler filtering and per-axis wrapping metadata;
- scene roots and node hierarchy with decomposed TRS transforms.

Unsupported features fail early instead of being silently ignored:
animations, skins, and morph targets are rejected by the loader. Extensions,
multiple UV sets, vertex colors, sparse accessors, material variants,
transmission, clearcoat, IBL asset import, animation, skinning, and streaming
remain future slices.

`cubey::engine` owns the current asset-to-scene bridge:

- `GltfSceneImportResources` stores app-owned mesh resources, material
  instances, material factors, uploaded material textures, and default PBR
  textures;
- `import_gltf_scene()` maps static glTF nodes into scene entities, 3D
  transforms, renderables, registry-issued mesh/material handles, imported
  bounds, and triangle counts;
- texture upload is deduplicated per glTF texture plus color space, and
  sampler `wrapS` / `wrapT` are preserved through Vulkan sampler axes;
- `destroy_gltf_scene_import()` tears down imported resources without making
  the engine own Vulkan texture or mesh lifetime globally.

`cubey::render` owns the reusable GPU-facing pieces:

- `PbrVertex`, `PbrSceneUniforms`, `PbrMaterialFactors`, and
  `PbrPushConstants` define the current shader contract;
- `pbr_forward_pass_info()` declares the scene uniform/shadow/IBL set, material
  texture set, push constants, and opaque/alpha forward pass state;
- `create_uploaded_texture_2d()` and `create_uploaded_texture_cube()` handle
  setup-time sampled texture uploads for glTF textures and generated IBL
  cubemaps;
- `create_generated_pbr_environment()` provides deterministic irradiance,
  prefiltered radiance, and DFG LUT resources for the current viewer
  checkpoint;
- `ShadowMapPass3D` owns a sampled depth texture plus depth-only pipeline for
  directional shadow passes.

`projects/gltf_viewer` is the integration project. It loads an input asset from
`--input`, falls back to the Khronos DamagedHelmet sample when the sample-assets
directory is configured, and otherwise renders a generated PBR cube. It creates
camera and light entities around imported bounds, records shadow and PBR scene
passes through the render graph, binds generated IBL resources into the PBR
scene material, supports opaque plus alpha forward pipelines, and can run
windowed or headless PNG capture. Its PBR shader uses the shared Cubey PBR
helper include for correlated Smith direct visibility, DFG-based IBL energy
compensation, and indirect specular occlusion.

## Boundaries

The asset loader stays CPU-only. It does not create entities, renderable
handles, textures, descriptors, pipelines, or scenes.

The engine importer is the current bridge between asset data and runtime scene
resources. It creates scene/render handles and app-owned resource tables, but
it does not choose shaders, record passes, allocate pipelines, or define
renderer-wide material policy.

The render layer exposes contracts and helpers, not a full material system.
Texture lifetime, descriptor writes, material sorting policy, alpha/shadow
policy, shader selection, and environment selection still belong to the project
or future renderer layer.

## Next Slices

- Add validation assets from Khronos Sample Assets as optional tests when the
  CI/dev environment can afford the download.
- Add alpha-mask support in the shadow pass instead of treating all shadow
  casters as opaque depth writers.
- Add HDR/KTX environment asset loading and offline or setup-time filtering now
  that the generated IBL path has proven the renderer-side cubemap contract.
- Add vertex colors, multiple UV sets, and glTF material extension slices as
  real sample assets require them.
