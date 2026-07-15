# Ocean Rendering

`projects/ocean` owns Cubey's local open-water renderer. It is a focused surface
system, not a planet, terrain, weather, or shallow-water owner.

The old comparison projects and experimental feature-isolation paths are gone.
Git history preserves them; active work should extend this renderer through
normal configuration, diagnostics, and small shared contracts.

## Runtime Shape

The renderer is split into these responsibilities:

1. `OceanConfig` and sea-state presets define the wave, material, LOD, and
   environment-coupling policy.
2. Enabled cascade slots generate spectra, evolve frequency fields, run FFTs,
   unpack displacement/normal/foam data, and maintain foam history.
3. `OceanSurfaceFrame` resolves the local tangent frame, camera altitude,
   horizon extent, effective mesh, projection range, datum, and curvature.
4. The clipmap renderer culls patches and samples the enabled wave fields in
   local XZ coordinates.
5. The surface shader combines water body, dielectric reflection, direct sun and
   moon light, foam, cloud products, self-shadowing, and far-field response.
6. Shared atmosphere/cloud runtimes render the background and provide lighting,
   reflection, exposure, and projected cloud transmittance.

The wave data remains local even when the base surface bends toward a spherical
datum. This keeps the FFT domain stable and makes planet/global coordinates a
future adapter concern.

## Wave Fields

There are five regular cascade slots. Sea-state presets enable C0 and C1 and
leave C2-C4 off. Every slot owns:

- enable state and resulting resource/dispatch cost;
- FFT map size and update interval;
- tile length, wind, fetch, swell, spread, and detail;
- displacement and normal contribution;
- whitecap threshold, foam amount, and optional spectral-domain cutoff.

Disabled slots do not allocate or dispatch the complete wave path. Contribution
strengths change presentation but do not remove compute cost.

The default 512 map and half-precision fields are the practical baseline. A 1024
full-precision run is a maximum-quality comparison, not a production default.

## Foam

Foam is not packed into the final normal result. The runtime preserves current
Jacobian breaking source, determinant/compression diagnostics, and accumulated
history. Final whitecaps combine current and persistent signals across enabled
slots, then apply sea-state density, sharpness, and lighting.

True plunging breakers, spray, and particle whitewater require geometry or
particles outside this heightfield surface and are deferred.

## Mesh And LOD

The active mesh is a camera-relative `ClipmapGrid2D` with automatic horizon
coverage. Camera altitude affects required half extent and target near-cell
size; frustum culling determines submitted rather than merely generated patch
and triangle counts.

Wave contribution follows both distance and representable mesh footprint:

- displacement fades when clipmap cells become too coarse;
- normal and foam detail have separate surface-distance support;
- far normal detail fades by estimated pixel footprint;
- diagnostics expose effective extent, cell size, patch load, and per-cascade
  shape/surface support at the horizon.

The shared `AdaptivePatchLod` planner should be considered only when planet
handoff, global address space, or shoreline topology creates a concrete need.
Replacing the local clipmap solely for architectural uniformity is not useful.

## Far Field

The far-field target is a low-contrast reflective ocean with subtle unresolved
swell and sun glitter, not distant resolved whitecap texture.

As wave detail becomes sub-pixel, the material converts unresolved energy into:

- modest roughness growth;
- broad low-frequency reflection variation;
- a view/light-dependent sun-glitter corridor;
- reduced residual normal detail.

Rejected spectral moment pyramids, synthetic far normals, and filtered
far-whitecap carriers exposed FFT tiling or added cost without improving the
accepted camera matrix. They are not runtime options.

## Environment Lighting

Ocean uses the shared atmosphere environment as the authoritative source for
background, sun/moon state, exposure, sky radiance, and broad reflection.
Clouds are rendered outside the water shader and consumed through bounded
products:

- projected transmittance for local direct-light shadows;
- a reflected planar cloud view for detailed visible-ocean reflection;
- a filtered cached environment for fallback coverage.

Planar plus cached fallback is the production reflection path. Cached-only mode
remains for diagnostics and consumers that need broad environment coverage.
Current-view and hybrid reflection modes were removed because their visibility
boundary produced missing or visibly mismatched reflection regions.

The water material remains ocean-specific. It is not the general forward-PBR
material, but it follows the same linear-HDR environment and exposure contract.

## Terrain Boundary

`TerrainOceanFieldView` defines height, water depth, shore signed distance, and
slope data. Ocean currently binds a synthetic diagnostic field and has a small
opt-in shoreline foam hook. It does not yet consume live terrain output.

Terrain should own bathymetry, shoreline distance, water masks, local datum,
and flow hints. Ocean should own open-water displacement, normals, foam, and
material response. Lakes may reuse parts of this renderer after those products
exist; rivers and shallow flow need a separate solver/model.

## Planet Boundary

The local renderer stops at horizon-scale and curved-local views. Planet-scale
navigation additionally needs global ocean topology, floating origin, spherical
patching, global weather, terrain/bathymetry streaming, and aerial/orbit cloud
products. Those remain `projects/planet` or future shared-adapter concerns.

The intended planet integration is to host the local ocean surface inside a
stable planet frame, not to grow this project into a second planet renderer.

## Diagnostics And Validation

The GUI is organized by Waves, Surface, Lighting, Scale & LOD, Environment, and
Diagnostics. Debug views inspect field outputs, LOD support, curvature,
reflections, cloud products, direct/ambient light, exposure, and terrain fields.

Use the canonical matrix for visual changes:

```sh
MOTION=0 projects/ocean/capture_ocean_review.sh outputs/ocean-review
```

Use `MOTION=1` when temporal stability, foam history, cloud motion, or changing
lighting is part of the change. The harness records the source commit and keeps
sea-state, scale, lighting, and diagnostic comparisons in one manifest.

See [Surface Ocean V1](../notes/ocean-surface-v1.md),
[Ocean visual captures](../notes/ocean-visual-captures.md), and
[Ocean performance](../notes/ocean-performance.md).
