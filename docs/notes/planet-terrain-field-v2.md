# Planet Terrain Field V2

The next planet slice should strengthen the procedural terrain and surface-field
contract before ocean is ported into `projects/planet`. The current surface is
useful enough for camera, LOD, atmosphere, and celestial validation, but it still
needs clearer terrain data products so later ocean, biome, cache, and streaming
layers do not have to reinterpret placeholder shader math.

## Direction

Keep the v2 terrain source project-local and procedural. This is still not a GIS
ingestion layer, out-of-core streamer, erosion simulation, or final art pass.
The goal is to make the same `face/level/x/y` patch identity produce stable
sample data and tile summaries that look like the boundary a real terrain source
will eventually implement.

The field vocabulary should stay explicit:

- height and height above sea level;
- water depth and normalized bathymetry;
- shoreline mask and land coverage;
- normal and normalized slope;
- moisture and temperature;
- roughness;
- material or biome classification.

These names intentionally follow established terrain/globe/rendering terms. They
are also the minimum data needed by future terrain materials, shoreline/ocean
handoff, weather/cloud masks, and cache residency diagnostics.

## Implementation Shape

`PlanetSurfaceSample` remains the point-sample contract. `PlanetSurfaceTileKey`
and `PlanetSurfaceTilePayload` remain the patch payload boundary. V2 expands
payload summaries with coverage, range, material-count, climate, roughness, and
terrain-relevant revision data instead of adding a new manager. A later
streaming system can replace the procedural source behind the same key/payload
vocabulary.

CPU and shader terrain logic should stay mirrored with matching helper names.
That duplication is deliberate for now: the CPU path gives deterministic tests
and tile summaries, while the shader path displaces the reusable patch grid at
interactive resolution. Divergence between those two paths should be treated as a
bug unless a later GPU terrain source makes the CPU path diagnostic-only.

The procedural shaping should use named layers rather than a single opaque noise
stack. The current procedural source follows this shape:

- ocean basin / continent mask;
- lowland breakup;
- mountain and ridge belts;
- fine detail gated by land/relief;
- moisture and temperature fields;
- material classification from sea level, elevation, slope, moisture, and
  temperature.

## Deferred

Keep the following out of this slice:

- real-world elevation, bathymetry, or satellite data;
- erosion, river networks, or watershed simulation;
- texture/biome asset streaming;
- ocean rendering as a planet layer;
- full terrain cache residency or async loading.

Ocean should attach after the planet field, LOD, atmosphere, and render-order
contracts are stable enough that it can consume terrain/bathymetry/shoreline
data without becoming the source of those contracts.
