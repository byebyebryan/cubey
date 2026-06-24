# Moon Surface Detail Plan

This note records the follow-up after moving visible moon rendering to shared
`CelestialBodyFrame` geometry. The old square lunar atlas was a near-side disk
projection and has been removed from live rendering. Final moon rendering and
the atmosphere `moon` / `moon-surface` debug views now use textured sphere
geometry.

## References

- NASA SVS CGI Moon Kit:
  <https://svs.gsfc.nasa.gov/4720>. Useful baseline for the runtime shape:
  color and elevation maps are flat longitude-latitude textures intended for
  spherical 3D rendering, with LRO WAC color and LOLA elevation provenance.
- Cesium Moon:
  <https://cesium.com/platform/cesium-ion/content/cesium-moon/>. Useful as a
  current production example for a complete lunar imagery/terrain body.
- USGS LROC WAC global morphology mosaic:
  <https://astrogeology.usgs.gov/search/map/moon_lro_lroc_wac_global_morphology_mosaic_100m>.
  Useful for global albedo/morphology structure and the expected polar/equator
  seam caveats in real lunar products.
- LROC nearside WAC mosaic:
  <https://lroc.im-ldi.com/images/293>. Useful as the current shape guide for
  named near-side maria: broad dark plains, separated by highland corridors, not
  one continuous black mass.
- PDS LRO LOLA archive:
  <https://pds-geosciences.wustl.edu/missions/lro/lola.htm>. Useful reference
  for elevation provenance and naming around laser-altimeter products.
- NASA PGDA SLDEM2015:
  <https://pgda.gsfc.nasa.gov/products/54>. Useful high-resolution topography
  reference for crater and mare relief scale.
- USGS Unified Geologic Map of the Moon:
  <https://astrogeology.usgs.gov/search/map/unified_geologic_map_of_the_moon_1_5m_2020>.
  Useful for broad mare/highland/geologic-region placement references, not as a
  direct runtime texture.

## Runtime Decision

Version 1 should stay procedural at runtime. The reference products above are
large and have their own import, storage, attribution, and update questions.
They should guide the visual model rather than become required runtime assets in
this batch.

The new visible moon surface should be a deterministic equirectangular texture:

- 2:1 longitude-latitude map, defaulting to `1024x512`.
- Metadata-bearing procedural artifact in `render.lunar_surface_map`.
- Generated from shared `cubey::procedural` seed/noise/hash utilities.
- Sampled in a stable moon-local body frame, not a camera-facing disk frame.

## Visual Targets

- Maria should be darker and broad, with soft edges and lower roughness.
- Highlands should stay brighter but muted, with dense small crater texture.
- Crater rays should be visible but rare and subtle.
- The terminator should reveal surface detail without noisy sparkle.
- Daylight washout should reduce contrast, not make the body disappear.
- Texture coordinates must not swim when the camera moves.

## Implementation Outcome

Implemented on the `sky-rendering` worktree in June 2026:

- `LunarSurfaceMap` is a deterministic `1024x512` equirectangular RGBA8
  procedural artifact generated from shared `cubey::procedural` seed, noise,
  hash, and metadata utilities. The current artifact formula is
  `lunar-surface-map-v13`.
- `CelestialBodyFrame` samples the surface in a stable moon-local body frame,
  so texture coordinates no longer face the camera.
- `AtmosphereBackgroundAtlasResources` now owns the visible lunar surface map
  and the night-sky atlas. The old square `LunarAtlas` and inline atmosphere
  moon disk path have been removed.
- Planet, standalone atmosphere final view, atmosphere `moon` / `moon-surface`
  debug views, fire/explosion, and water bind the surface map for geometry moon
  rendering.
- `CelestialBodyFrame` has a `SurfaceDebug` shading mode so moon-surface
  diagnostics inspect the same textured sphere path as final rendering. The
  debug view forces base-mip sampling so it does not hide broad maria behind an
  averaged texture mip.

The current captures show routing, phase behavior, and a close-up sphere debug
view. Maria are now broad body-space procedural fields instead of named
near-side basin primitives. The field is generated from lightly warped 3D FBM
in an oriented sphere domain, with a modest nearside bias and soft fill plateau
so the dark plains read closer to the generated face instead of collecting on
the limb. The v13 tuning widens that fill curve and gives the low-frequency
mare mass more weight so maria occupy more of the far-field disk. The texture
remains stable on the geometry moon without screen-space or UV painted stamp
artifacts. Surface tone uses separate normal-space FBM and
low-frequency body-space multipliers inspired by simple moon shader examples,
implemented with the existing Cubey procedural noise utilities rather than
copied Shadertoy noise code. Final acceptance should prioritize realistic
moon-size captures; the close-up sphere view is for seam, mip, and stability
checks. Crater scale and final-scene lighting balance remain material-tuning
follow-ups.
