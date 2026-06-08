# Planet Terrain Detail Batch

This note tracks the next long terrain-focused planet batch before ocean is
ported into `projects/planet`. The field vocabulary and first tile payload
boundary already exist; the remaining pressure is making surface view read as a
credible large-scale terrain system and defining where the local detail clipmap
belongs before it becomes a final-view participant.

## Direction

Keep the terrain source project-local and procedural. This is still not a GIS
ingestion layer, out-of-core streamer, erosion simulation, or final art pass.
The goal is to make the same `face/level/x/y` patch identity produce stable
sample data, credible material fields, and tile summaries that look like the
boundary a real terrain source will eventually implement.

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

## Current Shape

`PlanetSurfaceSample` remains the point-sample contract. `PlanetSurfaceTileKey`
and `PlanetSurfaceTilePayload` remain the patch payload boundary. Payload
summaries now include coverage, range, material-count, climate, roughness, and
averaged-field data instead of a new manager. A later streaming system can
replace the procedural source behind the same key/payload vocabulary. The
summary now carries height and sea-level ranges, water/shoreline coverage,
averaged height, averaged height above sea level, averaged moisture,
temperature, roughness, normalized slope, material counts, dominant material,
and terrain-relevant revision data.

CPU and shader terrain logic should stay mirrored with matching helper names.
That duplication is deliberate for now: the CPU path gives deterministic tests
and tile summaries, while the shader path displaces the reusable patch grid at
interactive resolution. Divergence between those two paths should be treated as a
bug unless a later GPU terrain source makes the CPU path diagnostic-only.

The procedural shaping should use named layers rather than a single opaque noise
stack. The current procedural source follows this shape:

- domain-warped terrain coordinates;
- ocean basin / continent mask;
- lowland breakup;
- mountain and ridge belts;
- valley network;
- fine detail gated by land/relief;
- moisture and temperature fields;
- material classification from sea level, elevation, slope, moisture, and
  temperature.

The global patch tree and local detail clipmap are intentionally separate:

- global patches own cube-sphere coverage, LOD identity, terrain/bathymetry
  sampling, and fallback coverage;
- local detail owns viewer-centered meter-scale geometry and should only be
  active when the camera is low enough that those triangles are visible;
- final rendering should stay on continuous global terrain until the local layer
  has a real local/global morph, persistent topology, or streaming ownership
  policy. The first final-view handoff exposed rectangular extent and noisy
  detail, so the clipmap is back to diagnostics and terrain-field inspection.

## Long Batch Result

The long batch is considered useful because:

- surface view now has stronger named landforms and less one-noise-stack
  character;
- terrain materials derive from elevation, slope, moisture,
  temperature, shoreline, and water depth;
- local detail diagnostics expose ownership and blend state without forcing the
  unfinished handoff into the default final view;
- LOD diagnostics report enough near-cell, active-level, and tile-summary data
  to explain terrain scale at the camera;
- docs and README clearly state what is now active, what remains deferred, and
  why ocean is still parked.

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
