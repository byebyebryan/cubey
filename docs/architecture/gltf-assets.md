# glTF Assets And PBR

Cubey has a narrow glTF 2.0 path for imported assets. The goal is not a
complete DCC/runtime pipeline yet; it is a foundation contract for common
runtime asset data, PBR material inputs, texture upload, scene import,
animation/deformation, and a viewer project that keeps pressure on the
renderer.

## Precedent

- glTF 2.0 is the asset interchange target. Cubey follows core mesh, node,
  image, sampler, and metallic-roughness material terminology rather than
  inventing project-specific names.
- Khronos glTF Sample Assets are the default external reference set. CMake can
  point at an existing checkout with `CUBEY_GLTF_SAMPLE_ASSETS_DIR` or fetch it
  with `CUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON`. The checked-in
  `dev-gltf-conformance` preset is the isolated, opt-in lane for the pinned
  checkout; ordinary `dev` configuration keeps the fetch disabled.
- Filament remains the practical reference for keeping CPU asset data, engine
  scene import, renderable resources, material instances, views, and renderer
  policy separate.

## Current Scope

`cubey::asset` owns CPU-side loaded asset data:

- glTF/glb parsing through `cgltf`;
- a metadata-only scene-bounds probe that resolves the selected scene and
  reachable node transforms from declared `POSITION` accessor min/max values;
- external buffers, data URIs, image buffer views, PNG/JPEG decode for glTF
  textures through `stb_image`, `KHR_texture_basisu` KTX2 texture payloads
  through the Basis Universal transcoder, and standalone Radiance HDR decode;
- triangle mesh primitives with position, normal, tangent, UV0, optional UV1,
  optional `COLOR_0`, optional `JOINTS_0` / `WEIGHTS_0`, sparse accessor
  expansion, and optional named morph target deltas from
  `mesh.extras.targetNames`;
- metallic-roughness PBR material factors and texture references, including
  texture coordinate selection and `KHR_texture_transform` metadata;
- `KHR_materials_ior`, `KHR_materials_specular`, `KHR_materials_clearcoat`,
  `KHR_materials_sheen`, `KHR_materials_anisotropy`,
  `KHR_materials_iridescence`, `KHR_materials_emissive_strength`, and
  `KHR_materials_unlit` controls;
- core glTF alpha modes: `OPAQUE`, `MASK`, and `BLEND`;
- sampler filtering and per-axis wrapping metadata;
- scene roots and node hierarchy with decomposed TRS transforms, plus explicit
  matrix-authored node transforms for import paths that need exact local matrix
  preservation;
- core glTF animations, skins, node skin bindings, node/mesh morph weights, and
  inverse bind matrices.

## Material Extension Contract

The importer may recognize more material-extension data than the renderer can
claim as complete. `extensionsRequired` is therefore deliberately restricted
to extensions whose loader path and rendered semantics are closed:

| Extension | Required-use status | Current boundary |
| --- | --- | --- |
| `KHR_texture_transform` | supported | UV0/UV1 transforms and texture-coordinate overrides are propagated to every current material slot. |
| `KHR_texture_basisu` | supported | KTX2 BasisU payloads transcode to BC7 or RGBA8, with sample-asset smoke coverage. |
| `KHR_materials_unlit` | supported | Base color, vertex color, texture transforms, and alpha policy bypass lighting. |
| `KHR_materials_emissive_strength` | supported | Strength is folded into HDR emissive radiance. |
| `KHR_materials_ior` | supported | Authored IOR is validated as exactly zero or finite and at least one, preserved through material packing, and checked by analytic loader coverage plus the deterministic white-IBL furnace. |
| `KHR_materials_specular` | supported | Factor and color inputs are validated before import, including factor-only and texture/transform paths; F0/F90 feed direct and split-sum IBL response, with deterministic furnace and Khronos `SpecularTest` capture coverage. |
| `KHR_materials_clearcoat`, `KHR_materials_sheen`, `KHR_materials_anisotropy`, `KHR_materials_iridescence` | partial | Their current texture/factor plumbing and approximate lobes remain available only for optional extension use. |

Partial extensions remain useful for renderer development and optional asset
inspection, but Cubey rejects them when an asset declares them required. Each
extension is promoted independently after analytic and deterministic sample
coverage closes its semantics. Loader and analytic witnesses live in focused
core tests, while the material-conformance CTest label separates rendered
checks from ordinary smoke: the opt-in `pbr_furnace --conformance-case` layouts
capture fixed 512px white-IBL IOR or specular specimens and inspect foreground
region/channel relationships rather than byte-identical images. The
pinned Khronos `SpecularTest` lane adds fixed manual directional lighting plus
half-intensity static IBL, no clouds, explicit exposure, and a front-on camera
for end-to-end model-grid/chromatic-response evidence through the shared forward
renderer; it is not a pixel oracle for the asset. Its bounded sphere-grid
samples cannot be satisfied by the sky/background. Failed semantic checks leave
their PNG artifact in the build tree for inspection.

Unsupported features fail early instead of being silently ignored:
unknown `extensionsRequired`, non-triangle primitive modes, texture coordinate
sets above UV1, additional skin influence sets, unsupported morph target
attributes, sparse index accessors, and extension-only animation paths are
rejected by the loader. Arbitrary additional UV/color sets, material variants,
non-Basis KTX2 payloads, Draco/meshopt compression, transmission, volume,
dispersion, glTF cameras/lights, glTF environment extensions, advanced
animation runtime features, and streaming remain future slices.

`cubey::engine` owns the current staged asset-to-scene bridge and renderer
instance service:

- `GltfSceneImportResources` stores app-owned mesh resources, material
  instances, material factors, uploaded material textures, default PBR textures,
  and per-import deformation resources;
- `prepare_gltf_scene()` is CPU-only: it validates and copies material, texture,
  mesh, node, morph, and skin data into a self-contained prepared product,
  including BasisU transcoding, bounds, and triangle counts;
- `build_gltf_scene_resident()` runs on the GPU owner and creates textures,
  material instances, static meshes, and deformation buffers without mutating
  `Engine` or `Scene` state;
- `activate_gltf_scene()` adopts one complete resident product into registry-
  issued mesh/material handles and maps glTF nodes into scene entities, 3D
  transforms, renderables, and per-node output mesh handles. Nodes authored
  with a glTF `matrix` activate as explicit affine `Transform3D` values so
  hierarchy evaluation and bounds use the authored matrix;
- `import_gltf_scene()` remains the blocking compatibility wrapper over those
  three phases for callers that do not need staged activation;
- glTF alpha modes map into explicit render material alpha policy: `MASK`
  stays depth-writing and shadow-casting with alpha cutoff, while `BLEND`
  renders forward-only with premultiplied source-over alpha blending and no
  depth writes;
- texture upload is deduplicated per glTF texture plus color space, sampler
  `wrapS` / `wrapT` are preserved through Vulkan sampler axes, and
  `KHR_texture_basisu` material textures transcode to BC7 when Vulkan BC
  compression is enabled or to RGBA8 as a fallback;
- material texture coordinate selection, `KHR_texture_transform`, and vertex
  colors are propagated into the PBR material uniform and vertex contracts;
- clearcoat, sheen, anisotropy, and iridescence texture bindings use fixed PBR
  descriptor slots with per-material flags so the common path avoids sampling
  absent textures;
- `destroy_gltf_scene_import()` tears down imported resources without making
  the engine own Vulkan texture or mesh lifetime globally;
- `RendererService` owns renderer instance lifetime, and
  `ForwardPbrRenderer3D` owns the reusable shadow map, skybox, forward PBR
  pipelines, HDR scene-color target, post pipeline, scene/skybox/post material
  descriptors, depth attachment, and render graph recording for a 3D PBR view.
  Per-frame rendering enters through `ForwardPbrRenderer3DFrameRequestInfo` and
  `ForwardPbrRenderer3DRenderRequest`, with mesh/material/deformation inputs
  grouped as `ForwardPbrRenderer3DSceneResources`.

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
  LUT resources, and the renderer can also accept explicit environment texture
  bindings from runtime sky reflection probes;
- `ShadowMapPass3D` owns a sampled depth texture plus depth-only pipeline for
  directional shadow passes.

`projects/gltf_viewer` is the integration project. With a resolved `--input`
asset (or configured Khronos DamagedHelmet sample), it first performs the
metadata-only bounds probe and publishes a complete wireframe-style indexed
loading cage framed from those authored rest-pose bounds. It falls back to a
neutral unit cage if probing fails; with no resolved input it retains the
generated solid PBR cube generation. The full asset then loads through the
shared staged lifecycle.
File loading and import preparation run on a CPU worker, residency runs on the
GPU owner, and the app atomically activates the complete scene generation at a
frame boundary. The previous generation remains renderable until activation and
retires only after its latest submission ticket. Optional terrain preparation
and residency travel with the asset generation so scene and backdrop publish
together. Headless capture uses the same path but calls `finish()` before frame
zero. If no input or configured sample is available, the generated cube remains
active. The viewer can use
`--environment path/to/env.hdr` or the optional fetched Filament
`lightroom_14b.hdr` sample for static HDR-backed IBL; by default it renders the
procedural atmosphere as the visible background and captures that atmosphere
through the shared atmosphere runtime for specular IBL. The same runtime owns
surface clouds: the forward renderer composites them after opaque scene/depth
rendering and before display post, while its cached cloud environment feeds PBR
reflections. Atmosphere diffuse lighting uses SH. It creates camera and light entities around imported bounds,
builds shadow and scene frame plans, and hands those plans plus scene resources
to an engine-owned `ForwardPbrRenderer3D` through the shared forward-PBR request
helper for pass recording. Its reusable
forward-PBR shader package under `shaders/cubey/forward_pbr` writes linear HDR
scene color and uses the shared Cubey PBR helper include for
base-color-to-diffuse/F0 remapping, dielectric IOR/specular controls,
correlated Smith direct visibility, DFG-based IBL energy compensation, and
indirect specular occlusion. The glTF PBR shader also evaluates the current
opaque material extension lobes: clearcoat, sheen, anisotropic GGX, and a
lightweight iridescence Fresnel tint. Material texture and factor alpha remain
straight/unassociated inputs; blended fragments emit premultiplied RGB at
shader output, while opaque and kept masked fragments output alpha 1. Display
transform is applied by the shared post shader. Unlit glTF materials preserve
the same alpha policy but skip direct lighting, IBL, normal mapping, AO,
metallic, roughness, and specular shading.
`--debug-view` can select forward-PBR material diagnostics at startup, and `D`
cycles the same views in the windowed viewer. The current views cover final
shading, base color, tangent-space and geometric normals, roughness, metallic,
occlusion, emissive, shadow, alpha, and UV0.
The viewer plays one active glTF animation clip, applies rigid TRS channels to
scene transforms, uploads morph weights and skin joint palettes per frame, and
records a compute deformation pass before shadow and PBR scene passes.
When Khronos Sample Assets are configured, optional headless compatibility
smokes cover material, texture transform, alpha, and tangent-space validation
scenes, including `SpecularTest`, `TextureTransformTest`,
`TextureTransformMultiTest`, `NormalTangentTest`, `NormalTangentMirrorTest`,
and `DamagedHelmet`. These tests carry the `gltf_sample` and `compatibility`
labels and only assert that the viewer can load and produce a valid PNG; they
are not golden-pixel comparisons. The `dev-gltf-conformance` test preset also
selects the `gltf` core/viewer evidence and `conformance` checks, including the
deterministic furnace IOR/specular captures and the semantic Khronos
`SpecularTest` capture when registered. It excludes tests labeled `windowed`.
The configured sample inventory checks every referenced path at configure time
and fails if the lane would otherwise register no sample tests. Downloaded
assets and generated captures stay under the isolated build tree.

Performance evidence is separate from those compatibility and conformance
checks. The release-only
`projects/gltf_viewer/profile_gltf_loading.sh` workflow profiles the staged
metadata probe, asset load, scene preparation, GPU residency, and activation
boundaries against a pinned five-asset Sample Assets corpus. It emits a
complete 19-metric `gltf_loading` generation set per observation and keeps
first-observation, warm-repetition, and cold-cache claims distinct. The current
baseline and decision rules live in
[`docs/notes/gltf-loading-profile.md`](../notes/gltf-loading-profile.md).

Cubey's tangent-space policy is validate-first. glTF recommends MikkTSpace for
missing tangents, and Filament exposes MikkTSpace generation as an optional
extended loader path, but Cubey keeps the current fallback tangent generator for
now. MikkTSpace is not a trivial swap because it can require unindexed tangent
output and vertex/index remapping instead of preserving authored index buffers.
The fallback generator still emits a complete glTF tangent vector by accumulating
both tangent and bitangent directions and deriving mirrored-UV handedness into
`tangent.w`. Its handedness matches glTF's upper-left UV origin and OpenGL-style
normal-map convention, which means the emitted `tangent.w` is opposite the raw
UV-derivative bitangent sign.
The `NormalTangentTest` and `NormalTangentMirrorTest` sample smokes are the
current pressure for deciding when that integration is worth doing.

## Boundaries

The asset loader stays CPU-only. It does not create entities, renderable
handles, textures, descriptors, pipelines, or scenes.

The scene-bounds probe is also CPU-only and intentionally does not load
external buffers or embedded GLB BIN chunks, decode images, transcode textures,
or construct materials. For GLB files it reads only the JSON chunk needed for
metadata parsing; textual `.gltf` files are read as JSON while their external
buffers remain unopened.
Its bounds are authored rest-pose bounds: morph and skin animation extremes
are not included. The viewer's loading cage is project-local indexed triangle
geometry made from twelve thin rectangular prisms, so it does not require
line-rasterization support or renderer-wide changes.

The engine importer is the current bridge between asset data and runtime scene
resources. Its preparation phase stays CPU-only, its residency phase creates
app-owned GPU resources, and only activation creates scene/render handles. It
does not choose shaders, record passes, allocate graphics pipelines, or define
renderer-wide material policy. `RendererService` owns renderer instance
lifetime only. `ForwardPbrRenderer3D` is a separate engine-layer renderer
implementation: it records one PBR view using the shared forward-PBR shader
package, caller-provided frame plans, scene resources, environment resources,
and render settings.

The render layer exposes contracts and helpers, not a full material system.
Texture lifetime, descriptor writes, shader selection, and environment
selection still belong to the project or future renderer layer. Transparency V1
supports glTF alpha mask and alpha blend, but not refraction, transmission,
transparent shadow opacity, weighted blended transparency, or order-independent
transparency. Transmission, volume absorption, dispersion, and other
transmissive glTF material extension lobes remain future slices.

## Next Slices

- Add the next model-fidelity import only when it stays within the current
  dependency boundary or clearly justifies a new one.
- Add prefiltered KTX/KTX2 environment loading separately from glTF material
  `KHR_texture_basisu`; IBL uses cubemaps and prefilter data, not the 2D
  material-texture upload path.
- Keep MikkTSpace tangent generation deferred until the tangent-space validation
  scenes or authored normal-map assets show visible tangent-basis artifacts.
