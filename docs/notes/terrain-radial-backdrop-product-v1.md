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
- 2.552 ms terrain-surface p95 at 2560 x 1440 under the maintained profile.

The performance result misses both the eventual `<1 ms` target and the advisory
`1.5 ms` checkpoint. It does not block this macro productization decision, but
it must not be described as a completed backdrop performance budget.

## Deferred Work

The immediate follow-up is terrain fidelity after integration: secondary source
detail, silhouette bandwidth, and procedural material response, evaluated in
close diagnostics but accepted at far-field distance. Separately measure and
optimize cache setup, persistence, sector/index policy, and GPU cost.

Hydrology, foliage, terrain streaming, traversable terrain, and an external
engine-level terrain API remain outside radial-v1.
