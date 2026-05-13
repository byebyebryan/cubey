# PBR And IBL Direction

Cubey's PBR path should use established glTF and Filament-style terminology:
metallic-roughness materials, image-based lighting, irradiance, GGX-prefiltered
radiance, a DFG lookup, and multiscatter energy compensation. The first goal is
a small renderer foundation that can light imported static assets plausibly
without taking on a complete material system.

## Current Direction

The current IBL checkpoint uses generated cubemaps instead of external
environment assets:

- `cubey::render` owns the texture and PBR descriptor contracts for cube maps,
  mip chains, samplers, and the DFG lookup texture;
- a generated environment helper creates deterministic diffuse irradiance,
  prefiltered specular radiance, DFG LUT byte data, and uploaded GPU resources;
- `gltf_viewer` binds those resources into the existing PBR scene material and
  lights the glTF PBR shader with Filament-style base-color remapping,
  DFG-based IBL, specular energy compensation, correlated Smith direct
  visibility, and indirect specular occlusion while keeping shader/pass
  selection project-owned;
- `pbr_furnace` isolates the current IBL/specular behavior with a white sphere
  grid that sweeps roughness across columns and metallic across rows under a
  uniform white environment;
- HDR equirectangular loading, KTX loading, offline convolution, environment
  selection UI, and glTF environment extensions remain future asset-pipeline
  slices.

## Renderer Contract

The first IBL contract is:

- irradiance cube: low-resolution diffuse lighting, one mip;
- prefiltered specular cube: roughness-addressed mip chain generated with a
  setup-time GGX importance-sampled convolution of the procedural radiance
  environment;
- DFG LUT: 2D lookup sampled by `NdotV` and roughness. Red/green store
  split-sum scale/bias terms, blue stores white-conductor single-scatter energy
  for compensation, and alpha remains one;
- scene uniforms: environment intensity plus prefiltered mip count;
- material factors and glTF textures remain in the material descriptor set.

The shared shader include remaps `baseColor` into `diffuseColor =
baseColor * (1 - metallic)` and `f0 = mix(0.04, baseColor, metallic)`.
Diffuse lighting uses `diffuseColor` directly; Fresnel-derived attenuation is
kept on the specular path, where the DFG blue channel provides the
single-scatter energy term used for multiscatter compensation.

This keeps the PBR shader contract close to common real-time renderer practice
while leaving HDR/KTX import, offline filtering, environment selection, and
renderer-wide material management explicit future work.

## Remaining Gaps

- Reflectance/IOR and glTF specular material extensions are still absent; the
  dielectric F0 is fixed at 4%.
- Color management is still minimal: no renderer-wide exposure, tone mapping,
  color grading, or HDR output policy.
- Generated IBL proves the descriptor and shader contract, but real
  environment asset import and higher-quality offline filtering remain future
  work.
- Additional material lobes such as clearcoat, sheen, anisotropy,
  transmission, and volume absorption are intentionally outside this
  checkpoint.

## Non-Goals

- Do not add HDR or KTX loading to the generated-IBL checkpoint.
- Do not turn `cubey::render` into a renderer singleton or material manager.
- Do not hide Vulkan layout transitions, descriptor writes, or pass ordering.
- Do not add glTF animation, skinning, or material extensions as part of IBL.
