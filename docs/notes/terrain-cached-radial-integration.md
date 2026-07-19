# Terrain Cached Radial Integration

Date: 2026-07-16

Status: radial stride 3 promoted as the far-field backdrop default; the strict
`<1 ms` performance target remains open.

## Decision History

The first cached-radial study reduced only render-index topology while keeping
the accepted radial macro composition and full baked source product. Stride 3
was visually sufficient across three seeds and six headings, but its measured
`1.338 ms` terrain-surface p95 missed the original `<1 ms` promotion gate. That
study therefore rejected automatic promotion at the time.

The product decision was later narrowed: ship the accepted far-field macro as a
usable terrain backdrop, preserve the performance miss as explicit debt, and
defer mid-field detail and optimization until after integration. The promoted
runtime is exact study code, not a visual reimplementation.

## Product Contract

`radial-v1` owns these settings:

- graduated `mountains-hierarchy-v2` source;
- seeds remain consumer-selectable;
- `32.768 km` outer radius;
- `6 km` low-relief foreground footprint;
- broad restoration over `1-24 km` and detail restoration over `5-30 km`;
- stride-3 render indices over the full baked source product;
- 500 m focus height;
- 100-1000 m orbit and 0-30 degree elevation;
- unrestricted yaw;
- continuous center by default, with explicit `consumer-owned` center support;
- setup-time source evaluation only.

The runtime rejects generic source, weathering, density, and quality overrides
that would make `radial-v1` cease to be a versioned product. Historical source
controls remain available through explicit `hard-cut-v1`.

## Evidence

Run the maintained product pack with:

```sh
projects/terrain/capture_radial_backdrop_product.sh
```

It writes `outputs/terrain/radial-backdrop-product-v1/` and validates:

- three seeds over six yaw headings;
- six exact PNG matches between production and the cached-radial study lane;
- 100 m / 0 degree, 400 m / 8 degree, and 1000 m / 30 degree camera cases at
  opposing headings;
- continuous and consumer-owned center modes;
- clay, normal, projected-edge, material-weight, and stage-ownership views;
- setup time, peak RSS, render capacity, source samples, and 1440p GPU timing.

The product path and study path match exactly at every parity heading. Runtime
telemetry now confirms stride 3, 607,200 render-triangle capacity, 2,657,280 baked
source samples, and a continuous default center.

The 1000 m / 30 degree endpoint can look down onto the quiet foreground and
move mountains out of frame. That is a legal controller endpoint, not a promise
that every pitch/radius combination produces an ideal composition. Consumers
may use a narrower pitch envelope without restricting yaw.

## Performance Caveat

The original study measured `1.338 ms` p95 at 2560 x 1440. During productization,
identical production and study submissions varied with GPU duty cycle:

- 30 fps low-duty profiles measured about `3.7 ms` p95;
- an isolated 120 fps active profile measured `1.35 ms` p95;
- the maintained 300-frame, 60-warmup, 120 fps product pack measured
  `2.552 ms` p95.

Geometry counts and product/study pixels remained identical. The timing spread
therefore does not indicate a productization regression, but it does mean a
desktop-GPU p95 gate is not reproducible until clock residency is controlled.
The maintained run measured `1.677 ms` mean and `1.517 ms` p50. Those are the
current product signals and pass the provisional `2 ms` mean/p50 checkpoint.
P95 remains recorded as tail telemetry. The engine's eventual `<1 ms` target
has not been met.

Setup and first frame measured `10.509 s` and `364,200 KiB` peak RSS in the
product pack. Cache persistence and asynchronous setup remain obvious future
work; this batch does not optimize them.

## Boundaries

This promotion is a far-field backdrop product, not general terrain:

- no mid-field or surface-scene fidelity claim;
- no foliage, hydrology, water, or traversable center;
- no terrain streaming or external engine-level terrain API;
- no claim that radial attenuation represents terrain truth;
- no source-detail work hidden inside productization.

The next terrain batch can improve source/geometry and material detail against
close diagnostic views, but acceptance remains anchored to the supported
far-field envelope. Performance work should be a separate measured batch.
