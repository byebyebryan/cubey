# Ocean Surface Foundation Promotion

Date: 2026-07-23

Status: implementation contract; promotion not yet complete.

## Decision

Extract the accepted horizon-scale open-water renderer into a shared
`OceanSurfaceRuntime`. The ocean project remains the reference laboratory and
must consume the same runtime. glTF Viewer is the first external consumer and
the promotion gate.

The first viewer integration is explicit, atmosphere-backed, and ocean-only.
It uses the accepted scene profile: Windy sea state, 512 half-precision fields,
two active cascades, curved far surface, footprint-adaptive shading, and cached
cloud/environment reflection. Terrain plus ocean composition is a later batch.

## Ownership Boundary

- `cubey::render` owns reusable ocean configuration, sea-state application,
  spectral and clipmap planning, horizon coverage, surface frames, patch
  culling, and footprint quality policy.
- `cubey::engine` owns FFT and surface GPU resources, simulation updates,
  environment bindings, target pipelines, and draw recording.
- `ForwardPbrRenderer3D` optionally composes an ocean surface into its shared
  HDR color and depth before foreground geometry.
- Consumers own cameras, clocks, water-datum placement, atmosphere/cloud
  runtimes, cloud-product generation, UI, capture policy, and quality choice.
- The ocean project additionally owns expert diagnostics, the size-reference
  pillar, planar-cloud reflection generation, terrain-field experiments, and
  profiling orchestration.

The runtime consumes atmosphere probes, cached cloud probes, optional planar
reflection, cloud shadow, and terrain-field bindings. It does not create or
schedule those products.

## glTF Viewer Contract

`--ocean-backdrop` enables the integration. The existing viewer path remains
unchanged and allocates no ocean resources when the option is absent.

The viewer requires `--pbr-environment-source atmosphere`. Supplying both an
ocean backdrop and a terrain heightfield is rejected until shoreline, overlap,
and shared water-mask contracts exist.

The viewer places sea level below the imported scene center. Without an
explicit `--ocean-foreground-height`, foreground height is
`max(20 m, 2 * scene bounding radius)`. Runtime UI may change visibility, sea
state, and foreground height without changing the imported scene.

The scene pass order is atmosphere, ocean, opaque/alpha foreground geometry,
depth-aware clouds, and one display transform. Ocean and foreground geometry
share scene depth. Foreground PBR receives a bounded lower-hemisphere ocean
reflection, but the ocean does not reflect foreground geometry in this
version.

## Non-Goals

- terrain/ocean coexistence, shorelines, bathymetry, surf, or water masks;
- underwater rendering, refraction, wakes, spray, or interaction;
- SSR or planar reflection of foreground geometry;
- aerial/orbit global-ocean topology;
- moving atmosphere/cloud generation into the ocean runtime;
- exposing the ocean project's full diagnostic and quality UI in glTF Viewer.

## Acceptance

The ocean project must retain fixed-input still and motion quality after
rewiring. Maintained stills require SSIM at least `0.995` against the
pre-extraction baseline and no visible seam, lighting, or temporal regression.
Close, low, and mid core p50 may regress by no more than `0.05 ms` or 5%,
whichever allowance is larger.

At `1600 x 900`, the matched glTF Viewer ocean increment must remain at most
`1.50 ms` p50 excluding shared atmosphere and visible-cloud composition. P95 is
diagnostic. The proof must cover model/ocean depth ordering, unrestricted orbit
motion, cached cloud reflection, cloud shadowing, sea-state changes, foreground
height changes, and the disabled path.

Promotion closes only after focused runtime/renderer/viewer tests, full serial
tests, headless PNG/video smokes, review captures, and a documented performance
verdict pass.
