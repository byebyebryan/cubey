# Terrain Raster Backdrop V1

Date: 2026-07-19

Status: complete. The accepted natural-raster study is now an opt-in terrain
product without a runtime terrain generator or a change to the asset-free
`radial-v1` default.

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
- a continuous center with seam-matched radial sampling from `0-3.2 km`;
- the unchanged logarithmic outer field through `16.384 km`;
- high-density source sampling with stride-3 render indices;
- `50/100/250 m` minimum/default/maximum orbit radii;
- `0/8/30` degree minimum/default/maximum elevation and unrestricted yaw;
- flat backdrop material, unit vertical scale, and no weathering.

The field must cover the complete centered placement search and selected render
disk. Failure to find a passing placement or retain strict finite-field coverage
is a load error. The manifest seed is authoritative; an explicit mismatched run
seed is rejected.

The seam-matched center redistributes the existing 96 center intervals so its
last source interval matches the first logarithmic outer interval. Decimated
angular vertices come from one global partition shared by the center and all
48 culling sectors. This changes neither source height nor placement; it avoids
an abrupt radial density step and stride-3 T-junctions at the `3.2 km` join.

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

## Result

The maintained pack is
`outputs/terrain/raster-backdrop-product-v1/`. It covers seeds `0`, `9012`, and
`12345` at six unrestricted headings, the complete `50-250 m` / `0-30` degree
camera envelope, stride-1 comparisons, and clay, normal, projected-edge, LOD,
and stage-ownership diagnostics. The exact product is stride 3 with `607,200`
render triangles and `2,657,280` source samples.

Acceptance review found one real topology defect before closure: each sector
restarted its stride-3 angular phase while the center used a global phase. The
meshes shared identical boundary vertices, so the old numeric seam check
passed, but their rendered edge partitions differed and exposed pinholes. A
rendered-edge regression test now covers that contract. The final clay and
surface packs retain continuous coverage with no cutout, spokes, holes, or
sector seams. Stride 3 preserves the intended far-field silhouette against the
stride-1 reference.

On the maintained machine at `2560 x 1440`, raster-v1 measured `1.073 ms` mean,
`1.025 ms` p50, and `1.299 ms` p95 terrain GPU time. The same run measured
radial-v1 at `1.418 ms` mean and `1.410 ms` p50, so the relative 10 percent gate
passed. Setup plus first frame at `640 x 360` took `9,851 ms` with `533,096 KiB`
peak process RSS. Setup persistence, streaming, translated coverage, and close
terrain remain deferred; the next product step is integration into one real
scene consumer.
