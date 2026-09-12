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
  shader model uses glTF base-color remapping, exact dielectric IOR controls,
  `KHR_materials_specular` factors/textures, clearcoat, sheen,
  anisotropy, iridescence, DFG-based IBL, specular energy compensation,
  correlated Smith direct visibility, indirect specular occlusion, environment
  rotation, exposure, and tone mapping from the per-frame render request. The
  reusable GLSL sources live in the shared forward-PBR shader package, and the
  shader-directory config helper maps that package to compiled shader output
  paths. Asset loading and environment selection stay project-owned. The
  renderer's public contract is request-shaped and includes a lightweight PBR
  debug-view selector for final, base color, normals, roughness, metallic,
  occlusion, emissive, shadow, alpha, and UV0 inspection; pipeline, shadow,
  graph, sampler, and attachment state stay internal to the engine
  implementation;
- the shared atmosphere runtime now produces a procedural visible background,
  direct light data, low-order diffuse irradiance SH, a runtime atmosphere
  reflection probe for specular IBL, and explicit environment bindings that use
  generated/HDR fallback resources for diffuse irradiance and DFG lookup.
  Atmosphere SH stores the Lambert-convolved response normalized by pi because
  PBR and backdrop consumers multiply that response by diffuse albedo directly;
  consumers must not add or remove another pi factor.
  The shared atmosphere draw shaders live under
  `shaders/cubey/atmosphere/`, while the atmosphere model include and
  reflection prefilter remain in `shaders/cubey/`.
  `Environment3D` can opt into SH diffuse ambient, while the reusable forward
  PBR renderer can bind either a complete generated/HDR environment or explicit
  environment texture bindings supplied by a project. Dynamic atmosphere
  reflections are published as coherent full cubemaps: two prefiltered cubes
  retain the previous and current generations, updates are rate-limited to 4 Hz
  by default, and consumers crossfade over the update interval. No consumer can
  observe a cube assembled from different time-of-day faces. Its render request
  can also carry an atmosphere-owned surface-cloud frame; final PBR views
  compose that product against HDR scene color and sampled scene depth before
  display post. `projects/ocean` also
  consumes the runtime reflection probe directly for water reflection while its
  bespoke ocean shader remains outside the full PBR material path;
- `gltf_viewer` resolves `pbr.environment_source` once into a project policy.
  Missing or explicit `atmosphere` preserves the full procedural product:
  visible atmosphere background, atmosphere SH diffuse lighting, atmosphere and
  cloud reflections, direct light, automatic exposure, time advancement, and
  atmosphere controls. Explicit `static` instead selects an IBL skybox with the
  chosen generated/HDR irradiance and prefiltered cubes for diffuse, specular,
  and transmission fallback. Static mode disables atmosphere SH, legacy ambient
  fill, procedural direct light, automatic exposure, all atmosphere/cloud
  resource lifecycle work, and atmosphere controls. Terrain and ocean backdrops
  remain atmosphere-only. This makes static captures independent of atmosphere
  time inputs while preserving environment rotation as a visible IBL operation;
- `pbr_furnace` isolates the current IBL/specular behavior with a white sphere
  grid that sweeps roughness across columns and metallic across rows under a
  uniform white environment. Its default grid is unchanged; opt-in `ior`,
  `specular`, `clearcoat`, `anisotropy`, `iridescence`, and `sheen` conformance
  layouts fix camera, lighting, linear output, and material specimens for
  relational capture checks;
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
  for compensation, and alpha stores Charlie sheen directional albedo;
- scene uniforms: environment intensity and prefiltered mip count;
- post uniforms: final exposure, tone-map, and output-encoding controls applied
  to the HDR scene color before writing the caller's target;
- material descriptor set: base-color, metallic-roughness, normal, occlusion,
  emissive, specular strength, specular color, clearcoat, clearcoat roughness,
  clearcoat normal, sheen color, sheen roughness, anisotropy, iridescence, and
  iridescence thickness textures plus a per-material uniform block for factors
  and optional texture-presence flags.

PBR diagnostics are intentionally renderer-facing rather than UI-owned for now.
`--debug-view` initializes the requested mode in `gltf_viewer` and
`material_cubes`, and `D` cycles the same enum interactively. Non-final debug
views skip skybox rendering so material channels are visible against the scene
clear color before the normal post transform.

The shared shader include remaps `baseColor` into `diffuseColor =
baseColor * (1 - metallic)` and computes dielectric F0 directly from glTF IOR
(`F0 = ((ior - 1) / (ior + 1))^2`). IOR zero preserves glTF's
specular-glossiness compatibility mode. `KHR_materials_specular` then supplies
the dielectric F0 color and F90 weight; both direct and image-based diffuse
lighting are attenuated by the remaining Fresnel energy. The DFG blue channel
provides the single-scatter energy term used for multiscatter compensation.
Per-draw push constants now carry only the model transform; material factors
live in the material descriptor set.
Optional extension textures still use fixed descriptor slots with default
fallback textures, but shader fetches are gated by per-material flags so the
common path does not sample absent extension textures.

## Clearcoat Contract

`KHR_materials_clearcoat` is closed for required use without introducing a
material graph or shader permutation. The importer rejects non-finite or
out-of-range factor and roughness values and the prohibited combinations with
`KHR_materials_unlit` and `KHR_materials_pbrSpecularGlossiness`. It preserves
independent factor, roughness, and normal texture references, per-slot UV
transforms, and authored normal scale. All three textures are linear data; the
factor reads red, roughness reads green, and the normal texture supplies its own
tangent-space coat normal. Without a coat-normal texture, the layer uses the
geometric normal rather than inheriting the base normal map.

The renderer models one fixed-IOR 1.5 layer (`F0 = 0.04`). A single Schlick
coat weight at the coat normal/view angle attenuates the complete underlying
response—including direct diffuse/specular/sheen, diffuse and specular IBL,
ambient fill, and emission—and weights the coat GGX response. Direct lighting
uses the coat's `D * V` lobe under that weight. Indirect lighting uses the DFG
LUT blue channel, which already stores the Fresnel-free white-conductor
single-scatter integral, so it does not apply a second Fresnel term. A zero
factor bypasses coat roughness/normal texture work and leaves the underlying
material unchanged. Authored roughness remains preserved in CPU/material data;
the shader applies the same `0.04` numerical evaluation floor used by the base
GGX path.

Focused tests cover defaults, finite range validation, the unlit exclusion,
required-extension acceptance, channel selection, linear color spaces, normal
scale, UV transforms, uniform packing, and the direct/IBL layering structure.
The opt-in deterministic furnace verifies zero-factor neutrality and distinct
enabled-coat roughness/layering response with tolerant region comparisons. The
pinned Khronos `ClearCoatTest` adds an end-to-end staged importer/viewer smoke
covering factor, roughness, coat texture and normal-map interactions; neither
rendered lane is a byte-identical cross-GPU golden.

## Anisotropy Contract

`KHR_materials_anisotropy` is closed for required use on the existing fixed
material descriptor layout. The importer validates finite strength in `[0, 1]`
and finite rotation, and rejects the same unlit and specular-glossiness
combinations as the specification. A primitive using the extension must provide
an authored tangent or allow Cubey to generate one. Generation selects the
effective normal-texture UV set first, then the anisotropy-texture UV set, then
UV0; missing UVs or conflicting normal/anisotropy UV sets are rejected instead
of producing a tangent frame that cannot satisfy both textures.

The linear anisotropy texture maps red/green from `[0, 1]` to a tangent-space
direction and uses blue to scale strength. Rotation composes with that direction.
Direct lighting uses the anisotropic GGX distribution and correlated Smith
visibility. The current isotropically prefiltered environment is sampled with a
Khronos-style bent-normal approximation, avoiding a second environment prefilter
or descriptor. Strength zero takes the original isotropic direct and IBL paths
exactly.

Focused loader and shader tests cover validation, exclusions, required-use
acceptance, UV-aware tangent generation, texture channel/transform plumbing, and
the explicit isotropic fallback. A deterministic furnace adds fixed directional
light above white IBL to verify zero-strength rotation neutrality and an enabled
orthogonal response. The pinned Khronos `AnisotropyBarnLamp` BasisU smoke covers
the staged importer and textured renderer path without acting as a pixel golden.

## Sheen Contract

`KHR_materials_sheen` is closed for required use on the fixed material
descriptor layout. The importer validates finite color components and roughness
in `[0, 1]`, and rejects the prohibited combinations with unlit and
specular-glossiness materials. It preserves independently transformed textures:
the sheen color texture is sRGB RGB data and the sheen roughness texture reads
linear alpha. Authored zero roughness is preserved in material data; shader
evaluation applies only a numerical floor.

Direct lighting uses the Charlie distribution and full Estevez-Kulla
visibility. The DFG LUT alpha channel stores Charlie directional albedo `E` from
a deterministic 128-sample cosine-QMC integration; against the retained
512-sample reference points its maximum and mean absolute errors are bounded by
`0.02` and `0.005`. The direct base response is scaled by the minimum of its
view and light attenuation, while IBL and ambient fill use view attenuation.
The sheen IBL response reuses the existing GGX-prefiltered environment at sheen
roughness as a resource-constrained radiance approximation rather than adding a
second prefiltered cube.

Zero sheen color takes the previous base path exactly. Enabled sheen remains
beneath clearcoat and does not attenuate emission. Focused tests cover ranges,
exclusions, required-use acceptance, texture channels and transforms, the full
visibility function, DFG integration, and composition. A four-specimen furnace
checks zero-color roughness neutrality plus bounded enabled color and roughness
response. The pinned Khronos `SheenTestGrid` sample adds an end-to-end staged
importer/viewer smoke; neither rendered lane is a byte-identical cross-GPU
golden.

## Iridescence Contract

`KHR_materials_iridescence` is closed for required use on the fixed material
descriptor layout. The importer validates a finite factor in `[0, 1]`, a finite
IOR of at least one, and finite nonnegative minimum and maximum thicknesses. It
preserves independently transformed linear textures: the factor reads red and
the thickness interpolation reads green. Minimum above maximum remains valid,
matching the extension's interpolation contract. The prohibited combinations
with unlit and specular-glossiness materials are rejected.

The shared shader evaluates the Khronos thin-film interference model at the
normal/view angle. Dielectrics use the specular-adjusted dielectric F0 and
metals use base color for the film/base interface. Direct lighting replaces the
ordinary Fresnel mix with the thin-film response by factor; IBL performs the
corresponding RGB diffuse/specular mix exactly once. The result remains below
the clearcoat layer and preserves anisotropy's bent reflection direction.
Factor zero and zero thickness retain the pre-existing base response exactly.

Focused tests cover defaults, ranges, exclusions, required-use acceptance,
texture channels/transforms, and shader composition. A four-specimen furnace
checks zero-factor thickness neutrality plus visible dielectric and metallic
chromatic response under a fixed direct-plus-IBL fixture. The pinned Khronos
`CompareIridescence` sample adds an end-to-end staged importer/viewer smoke;
neither rendered lane is a byte-identical cross-GPU golden.

## Transmission, Volume, And Dispersion Contract

`KHR_materials_transmission` is independent from glTF alpha coverage. A positive
factor routes the material through an explicit transmission stage after opaque
geometry and surface clouds; ordinary alpha-blended geometry remains later in
the frame. The renderer snapshots the pre-transmission linear HDR scene into a
per-frame-slot radiance pyramid only when a visible transmissive packet needs
it. Thin transmission replaces the material's diffuse contribution with
base-color-tinted scene radiance while preserving reflected specular, sheen,
clearcoat, iridescence, emissive, AO, and premultiplied alpha behavior. Near or
beyond screen edges, transmission samples the selected environment's prefiltered
fallback: the current procedural atmosphere/cloud probe in atmosphere mode or
the same static generated/HDR environment in static mode.

`KHR_materials_volume` adds a linear green-channel thickness texture,
mesh-space thickness, world-space attenuation distance, and attenuation color.
A nonzero thickness derives a refracted entry ray, advances through the authored
mesh-space thickness, constructs a solid-volume/sphere-style approximation of a
second interface, and projects the estimated exit point into the same HDR
pyramid. Beer-Lambert absorption uses the resulting world-space path length;
the representative 546.1nm ray supplies that distance for dispersion. If the
approximate exit interface reaches total internal reflection, a reflected
environment fallback keeps the sample finite. Zero thickness retains the thin
one-sample path exactly. A nonzero thickness is treated as an exterior
closed-volume boundary, so glTF `doubleSided` does not alter its back-face
culling policy. This is an approximate exit model, not exact mesh back-face
depth reconstruction.

`KHR_materials_dispersion` performs deterministic four-wavelength integration
only for nonzero dispersion on the thick-volume path. Four fixed wavelength
samples use Filament-derived color-matching matrices and independent
screen/environment lookups, with no temporal or spatial jitter; the 546.1nm
sample supplies the representative attenuation distance. A zero factor
preserves the one-sample volume path. Transmission roughness first applies the
IOR-aware perceptual remapping, maps microfacet variance into the available 2x
HDR-pyramid mip chain, and uses the same remapped roughness for the environment
fallback. An authored smooth interface remains at exact mip zero.

Focused tests cover defaults, ranges, prohibited/dependent extension
combinations, texture channels/transforms, descriptor and uniform ABI, optical
routing, attachment synchronization, and shader composition. Pinned Khronos
transmission, thin-wall, attenuation, and dispersion scenes provide end-to-end
runtime smokes. A required-validation 512px `DragonDispersion` capture exercises
the atmosphere-backed product path. These screen-space captures are diagnostic
evidence rather than cross-GPU pixel goldens. The intentional first version has
no back-face exit-depth reconstruction, multiple internal reflection, punctual
BTDF, transparent shadows, or order-independent transparency.

## IOR And Specular Conformance

`KHR_materials_ior` accepts only IOR zero (the glTF compatibility sentinel) or
a finite value at least one. Cubey preserves the authored value, including
high values such as 2.42, and computes the dielectric normal-incidence endpoint
from it. `KHR_materials_specular` accepts a finite factor in `[0, 1]` and a
finite nonnegative color without silently clamping malformed data. Its factor
and color form the dielectric F0 endpoint while the factor supplies dielectric
F90; metallic materials continue to use base color and unit F90. Both direct
diffuse and diffuse IBL lose the Fresnel-reflected energy, while split-sum IBL
uses the material F0/F90 endpoints.

Focused core tests run the CPU/loader witnesses. The `conformance` CTest label
runs opt-in 512px white-IBL furnace captures with tolerant region and channel
relationships and—when the pinned Khronos assets are configured—a fixed manual
directional-light, half-intensity static-IBL/no-cloud 512px `SpecularTest`
capture through `gltf_viewer`. The front-on capture camera makes the sample's
dark mirror specimens inspectable while retaining the shared direct-light path;
its bounded sphere-grid witnesses verify useful model framing and chromatic
material response rather than treating the sky or every sample cell as a pixel
oracle. These artifacts are retained on semantic-check failure and are
deliberately not cross-GPU byte goldens.

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
- Atmosphere-driven PBR now covers procedural visible background, direct light,
  diffuse SH, a low-resolution runtime atmosphere reflection probe, direct
  surface-cloud composition, and a filtered cloud reflection environment in
  `gltf_viewer`. `projects/ocean` uses the shared atmosphere
  background plus the same runtime sky/reflection probes for water reflection,
  fog, and fill, but its water material remains bespoke rather than a full PBR
  surface. The runtime is intentionally V1: it keeps the static/generated
  diffuse irradiance and DFG resources as fallback foundation pieces, uses SH for
  atmosphere diffuse lighting, updates all reflection faces coherently on first
  use, skips unchanged environment assignments, coalesces changes between probe
  ticks, and atomically publishes complete replacement generations. The same
  runtime advances its cloud environment so time-of-day changes do not reset the
  cloud probe's previous/current interpolation.
- Sheen IBL reuses the existing GGX-prefiltered environment instead of a
  dedicated Charlie-prefiltered cube. Its uniform-environment energy is covered,
  but directional quality remains an explicit approximation; add the extra
  resource only if a real asset demonstrates the need.
- Transmission remains a screen-space approximation. Its thick-volume path uses
  a bounded solid-volume/sphere-style second-interface estimate rather than
  exact mesh back-face exit depth, and it does not model multiple internal
  reflection. Punctual transmitted-light BTDF, transparent shadows, and
  OIT-quality transparent composition remain future work.

## Non-Goals

- Do not add KTX loading or an offline IBL baking pipeline to the direct-HDR
  checkpoint.
- Do not fold exact refraction-depth reconstruction, ray tracing, or an OIT
  redesign into the first screen-space transmission contract.
- Do not turn `cubey::render` into a renderer singleton or material manager.
- Do not hide Vulkan layout transitions, descriptor writes, or pass ordering.
- Do not add glTF animation, skinning, or additional material extensions as
  part of IBL.
