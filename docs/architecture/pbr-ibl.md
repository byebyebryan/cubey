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
  PBR forward rendering of a caller-provided 3D frame plan. It renders glTF PBR
  and skybox shading into a linear HDR scene color target, then applies
  exposure, tone mapping, and output encoding in a fullscreen post pass. The
  shader model uses Filament-style base-color remapping, factor-only IOR
  controls, `KHR_materials_specular` factors/textures, clearcoat, sheen,
  anisotropy, iridescence, DFG-based IBL, specular energy compensation,
  correlated Smith direct visibility, indirect specular occlusion, environment
  rotation, exposure, and tone mapping from the
  per-frame render request while keeping asset loading, shader paths, and
  environment selection project-owned;
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
- scene uniforms: environment intensity and prefiltered mip count;
- post uniforms: final exposure, tone-map, and output-encoding controls applied
  to the HDR scene color before writing the caller's target;
- material descriptor set: base-color, metallic-roughness, normal, occlusion,
  emissive, specular strength, specular color, clearcoat, clearcoat roughness,
  clearcoat normal, sheen color, sheen roughness, anisotropy, iridescence, and
  iridescence thickness textures plus a per-material uniform block for factors
  and optional texture-presence flags.

The shared shader include remaps `baseColor` into `diffuseColor =
baseColor * (1 - metallic)` and computes dielectric F0 from Filament-style
reflectance (`F0 = 0.16 * reflectance^2`) plus glTF
`KHR_materials_specular` controls before mixing toward metallic `baseColor`.
Diffuse lighting uses `diffuseColor` directly; Fresnel-derived attenuation
stays on the specular path, where the DFG blue channel provides the
single-scatter energy term used for multiscatter compensation. Per-draw push
constants now carry only the model transform; material factors live in the
material descriptor set.
Optional extension textures still use fixed descriptor slots with default
fallback textures, but shader fetches are gated by per-material flags so the
common path does not sample absent extension textures.

The current display transform is intentionally small: exposure in stops, a
linear-or-ACES tone-map selector, and an output-encoding selector. The reusable
forward PBR renderer applies it in a post pass after shading into an
`R16G16B16A16_SFLOAT` scene color target. Windowed swapchains prefer sRGB
attachment formats, so the post shader leaves encoding to the attachment when
possible. UNORM final targets, including headless PNG capture, request
shader-side linear-to-sRGB encoding.

This keeps the PBR shader contract close to common real-time renderer practice
while leaving KTX import, offline filtering, environment selection UI, and
renderer-wide material management explicit future work.

## Remaining Gaps

- Color management is still minimal: the reusable PBR renderer now has an HDR
  scene color target and fullscreen post pass, but no color grading, bloom,
  HDR10/output-device policy, or automatic exposure.
- The current HDR path performs setup-time CPU filtering. It is useful for
  development and material inspection, but higher-quality offline filtering and
  prefiltered KTX/KTX2 deployment remain future work.
- The current clearcoat, sheen, anisotropy, and iridescence lobes are pragmatic
  real-time approximations. Transmission, refraction, volume absorption,
  dispersion, and OIT-quality transparent material behavior remain future work.

## Non-Goals

- Do not add KTX loading or an offline IBL baking pipeline to the direct-HDR
  checkpoint.
- Do not add transmission, volume, dispersion, or other transmissive extension
  textures to the material descriptor set yet.
- Do not turn `cubey::render` into a renderer singleton or material manager.
- Do not hide Vulkan layout transitions, descriptor writes, or pass ordering.
- Do not add glTF animation, skinning, or additional material extensions as
  part of IBL.
