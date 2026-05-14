# PBR And IBL Direction

Cubey's PBR path should use established glTF and Filament-style terminology:
metallic-roughness materials, image-based lighting, irradiance, GGX-prefiltered
radiance, a DFG lookup, and multiscatter energy compensation. The first goal is
a small renderer foundation that can light imported static assets plausibly
without taking on a complete material system.

## Current Direction

The current IBL checkpoint supports both deterministic generated cubemaps and
Radiance HDR equirectangular environment assets:

- `cubey::render` owns the texture and PBR descriptor contracts for cube maps,
  mip chains, samplers, and the DFG lookup texture;
- a generated environment helper creates deterministic diffuse irradiance,
  prefiltered specular radiance, DFG LUT byte data, and uploaded GPU resources;
- an HDR image loader decodes Radiance `.hdr` images into linear `RGBA32F`
  CPU data, and the render layer can build setup-time irradiance and
  GGX-prefiltered cubemaps from equirectangular radiance;
- `RendererService` owns renderer instance lifetime, and
  `ForwardPbrRenderer3D` binds those resources for reusable shadow, skybox, and
  PBR forward rendering of a caller-provided 3D frame plan. It lights the glTF
  PBR shader with Filament-style base-color remapping, factor-only IOR/specular
  material controls, DFG-based IBL, specular energy compensation, correlated
  Smith direct visibility, indirect specular occlusion, environment rotation,
  and exposure while keeping asset loading, shader paths, and environment
  selection project-owned;
- `pbr_furnace` isolates the current IBL/specular behavior with a white sphere
  grid that sweeps roughness across columns and metallic across rows under a
  uniform white environment;
- optional Filament sample HDR environments can be fetched by CMake for local
  inspection, with `lightroom_14b.hdr` as the default viewer environment when
  available;
- KTX loading, offline convolution, environment selection UI, and glTF
  environment extensions remain future asset-pipeline slices.

## Renderer Contract

The first IBL contract is:

- irradiance cube: low-resolution diffuse lighting, one mip;
- prefiltered specular cube: roughness-addressed mip chain generated with a
  setup-time GGX importance-sampled convolution of either the generated
  radiance environment or an equirectangular HDR image;
- DFG LUT: 2D lookup sampled by `NdotV` and roughness. Red/green store
  split-sum scale/bias terms, blue stores white-conductor single-scatter energy
  for compensation, and alpha remains one;
- scene uniforms: environment intensity, prefiltered mip count, and final
  display transform controls;
- material descriptor set: base-color, metallic-roughness, normal, occlusion,
  and emissive textures plus a per-material uniform block for factors.

The shared shader include remaps `baseColor` into `diffuseColor =
baseColor * (1 - metallic)` and computes dielectric F0 from Filament-style
reflectance (`F0 = 0.16 * reflectance^2`) plus factor-only glTF
`KHR_materials_specular` controls before mixing toward metallic `baseColor`.
Diffuse lighting uses `diffuseColor` directly; Fresnel-derived attenuation
stays on the specular path, where the DFG blue channel provides the
single-scatter energy term used for multiscatter compensation. Per-draw push
constants now carry only the model transform; material factors live in the
material descriptor set.

The current display transform is intentionally small: exposure in stops, a
linear-or-ACES tone-map selector, and an output-encoding selector. Windowed
swapchains prefer sRGB attachment formats, so shaders leave encoding to the
attachment when possible. UNORM final targets, including headless PNG capture,
request shader-side linear-to-sRGB encoding.

This keeps the PBR shader contract close to common real-time renderer practice
while leaving KTX import, offline filtering, environment selection UI, and
renderer-wide material management explicit future work.

## Remaining Gaps

- glTF specular textures are still absent; only factor-only
  `KHR_materials_ior` and `KHR_materials_specular` are imported.
- Color management is still minimal: the PBR path now has a display-transform
  contract, but no HDR scene color target, fullscreen present pass, color
  grading, or HDR output policy.
- The current HDR path performs setup-time CPU filtering. It is useful for
  development and material inspection, but higher-quality offline filtering and
  prefiltered KTX/KTX2 deployment remain future work.
- Additional material lobes such as clearcoat, sheen, anisotropy,
  transmission, and volume absorption are intentionally outside this
  checkpoint.

## Non-Goals

- Do not add KTX loading or an offline IBL baking pipeline to the direct-HDR
  checkpoint.
- Do not add specular, clearcoat, transmission, or other extension textures to
  the material descriptor set yet.
- Do not turn `cubey::render` into a renderer singleton or material manager.
- Do not hide Vulkan layout transitions, descriptor writes, or pass ordering.
- Do not add glTF animation, skinning, or additional material extensions as
  part of IBL.
