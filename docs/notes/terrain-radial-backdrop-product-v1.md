# Terrain Radial Backdrop Product V1

Date: 2026-07-16

Status: implemented and default for cached backdrop cameras.

## Purpose

Radial-v1 turns the accepted terrain study into a named runtime product without
claiming to solve general terrain. It gives rendering projects a deterministic
far-field mountain environment while preserving explicit boundaries for scene
foregrounds, future detail work, and performance optimization.

## Runtime Ownership

The profile owns its source, radial composition, cache domain, camera stage,
mesh density, stride, and presentation defaults. Runtime code uses the same
graduated source evaluator and radial operators as the study. The study
executables remain explicit hard-cut/injected-source controls.

Default invocation:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset backdrop-stage
```

Consumer-owned foreground invocation:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset backdrop-stage \
  --terrain-backdrop-center consumer-owned
```

Historical hard-cut invocation:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-backdrop-profile hard-cut-v1 \
  --terrain-preset mountain \
  --terrain-camera-preset backdrop
```

## Validation

`projects/terrain/capture_radial_backdrop_product.sh` is the maintained product
pack. It requires exact study parity and records visual, topology, ownership,
setup, memory, and GPU evidence. Review
`outputs/terrain/radial-backdrop-product-v1/REVIEW.md` in the stated order.

The 2026-07-16 pack recorded:

- six of six exact product/study PNG pairs;
- three seeds across six yaw headings;
- no visible sector holes or radial transition ring in supported scene views;
- stride 3 and 607,232 render-triangle capacity;
- 2,657,280 setup-time source samples;
- 10.509 s setup/first-frame and 364,200 KiB peak RSS;
- 1.677 ms mean, 1.517 ms p50, and 2.552 ms p95 for terrain surface at
  2560 x 1440 under the maintained profile.

Mean and p50 pass the current `2 ms` product checkpoint. P95 remains tail
telemetry because it varies with GPU power-state residency. The result still
misses the eventual `<1 ms` engine target and must not be described as a
completed backdrop performance budget.

## Topology A/B Verdict

The first GUI review made peak edges and broad faces read as low polygon count.
That diagnosis was tested directly instead of immediately adding runtime LOD.
Run `projects/terrain/capture_radial_lod_ab.sh` to reproduce the 1440p pack in
`outputs/terrain/radial-lod-ab-v1/`. The pack varies only the cached radial
render-index stride and includes orbit overviews, lossless fixed-angle controls,
projected-edge diagnostics, and GPU profiles at both the 100 m stress distance
and 400 m product distance.

In the recorded run:

- stride 1 submitted 1,668,096 triangles across 11 visible sectors;
- stride 2 submitted 419,328 triangles across the same sectors;
- stride 3 submitted 190,464 triangles across the same sectors;
- stride 1 versus stride 3 final surface RMSE was about `0.17%` at both focused
  distances and produced no meaningful silhouette improvement;
- the projected-edge diagnostic exposed larger local differences, but those
  differences did not survive final shading at normal inspection scale;
- stride 1 measured `1.844 ms` mean and `1.858 ms` p50, versus stride 3 at
  `1.299 ms` mean and `1.260 ms` p50 in this active-clock run.

The fixed-control result rejects insufficient submitted topology as the main
cause of the current rough or faceted read. The dominant limits are the broad
baked source shape and low-bandwidth normal/material response. Keep stride 3
for radial-v1 and move the next visual pass to those inputs. A projected-error
LOD policy remains relevant when the camera envelope expands or when triangle
work must be redistributed, but it is not expected to improve the current
product-distance image by itself.

## Deferred Work

The immediate follow-up is terrain fidelity after integration: more coherent
secondary source relief plus higher-bandwidth procedural normal and material
response, evaluated in close diagnostics but accepted at far-field distance.
Separately measure and optimize cache setup, persistence, sector/index policy,
and GPU cost.

Hydrology, foliage, terrain streaming, traversable terrain, and an external
engine-level terrain API remain outside radial-v1.
