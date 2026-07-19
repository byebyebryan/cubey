# Terrain Raster Backdrop V1

Date: 2026-07-19

Status: implementation contract. Promote the accepted natural-raster study into
an opt-in terrain product without adding a runtime terrain generator or changing
the asset-free `radial-v1` default.

## Product Goal

`raster-v1` is a fixed-focus, far-field terrain backdrop backed by one offline
regular heightfield. It carries the stronger mountain morphology demonstrated by
the natural-raster study through the existing cached backdrop renderer.

This product is not traversable terrain. It does not promise close or midfield
detail, translated worlds, streaming, hydrology, vegetation, water, or generator
execution. Those concerns must not expand this promotion batch.

## Asset Boundary

The runtime consumes an explicit `cubey.terrain.heightfield.v1` manifest and one
little-endian, row-major float elevation file. The manifest owns grid dimensions,
world-space sample spacing and origin, height offset and scale, source identity,
seed, and relief metadata. Extra provenance is allowed, but climate data is not a
runtime dependency.

Generated binaries remain external to Git. Terrain Diffusion is one offline
producer, not part of the runtime contract. Existing study fields may be repacked
by writing the product manifest beside their unchanged elevation data; model
execution and elevation copying are unnecessary.

V1 loads the complete elevation field and builds its filtered mip pyramid during
setup. Persistence, asynchronous loading, and tiled streaming remain deferred.

## Frozen Profile

Selecting `terrain.backdrop_profile=raster-v1` requires an explicit
`terrain.heightfield` path and a backdrop camera. The profile owns:

- deterministic natural directional placement over the unchanged source;
- a `500 m` focus above the selected terrain center;
- a continuous center with uniform radial sampling from `0-3.2 km`;
- the unchanged logarithmic outer field through `16.384 km`;
- high-density source sampling with stride-3 render indices;
- `50/100/250 m` minimum/default/maximum orbit radii;
- `0/8/30` degree minimum/default/maximum elevation and unrestricted yaw;
- flat backdrop material, unit vertical scale, and no weathering.

The field must cover the complete centered placement search and selected render
disk. Failure to find a passing placement or retain strict finite-field coverage
is a load error. The manifest seed is authoritative; an explicit mismatched run
seed is rejected.

`radial-v1` remains the default when no external field is supplied. Raster-v1
does not silently fall back to the procedural source.

## Acceptance

The product must retain the study's zero center/outer seam and exact cached
topology budget. Review covers all three maintained fields, six headings, and
the orbit-radius/elevation endpoints. Surface, clay, normal, projected-edge, and
LOD diagnostics must show no cutout, radial spokes, holes, or sector seams.

Stride 3 must preserve the intended far-field silhouette against stride 1. On
the same machine and capture settings, raster-v1 p50 and mean terrain GPU time
must remain within 10 percent of radial-v1. Setup duration and peak RSS are
recorded, not optimized in this batch; the earlier sub-millisecond aspiration
remains deferred.

The next gate after this product passes is one real external consumer. That work
will determine whether renderer ownership should move from `projects/terrain`
into an engine-level module.
