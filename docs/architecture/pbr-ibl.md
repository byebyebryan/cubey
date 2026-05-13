# PBR And IBL Direction

Cubey's PBR path should use established glTF and Filament-style terminology:
metallic-roughness materials, image-based lighting, irradiance, prefiltered
radiance, and a split-sum BRDF lookup. The first goal is a small renderer
foundation that can light imported static assets plausibly without taking on a
complete material system.

## Current Direction

The current IBL checkpoint uses generated cubemaps instead of external
environment assets:

- `cubey::render` owns the texture and PBR descriptor contracts for cube maps,
  mip chains, samplers, and the BRDF lookup texture;
- a generated environment helper creates deterministic diffuse irradiance,
  prefiltered specular radiance, BRDF LUT byte data, and uploaded GPU
  resources;
- `gltf_viewer` binds those resources into the existing PBR scene material and
  lights the glTF PBR shader with a split-sum-style ambient term while keeping
  shader/pass selection project-owned;
- `pbr_furnace` isolates the current IBL/specular behavior with a white sphere
  grid that sweeps roughness across columns and metallic across rows under a
  uniform white environment;
- HDR equirectangular loading, KTX loading, offline convolution, environment
  selection UI, and glTF environment extensions remain future asset-pipeline
  slices.

## Renderer Contract

The first IBL contract is:

- irradiance cube: low-resolution diffuse lighting, one mip;
- prefiltered specular cube: roughness-addressed mip chain;
- BRDF LUT: 2D lookup sampled by `NdotV` and roughness;
- scene uniforms: environment intensity plus prefiltered mip count;
- material factors and glTF textures remain in the material descriptor set.

This keeps the PBR shader contract close to common real-time renderer practice
while leaving HDR/KTX import, offline filtering, environment selection, and
renderer-wide material management explicit future work.

## Non-Goals

- Do not add HDR or KTX loading to the generated-IBL checkpoint.
- Do not turn `cubey::render` into a renderer singleton or material manager.
- Do not hide Vulkan layout transitions, descriptor writes, or pass ordering.
- Do not add glTF animation, skinning, or material extensions as part of IBL.
