# Terrain Radial Backdrop Macro Baseline

Date: 2026-07-16

Status: accepted macro-composition baseline; the first continuous-center cached
integration failed the runtime gate; not yet a production renderer or final
terrain source.

## Decision

Freeze `expanded-radial` v2 as the terrain backdrop's macro-composition target.
It is the first tested composition that keeps a useful foreground, restores
distant mountains gradually, and remains useful through unrestricted yaw
without exposing a hard cut or one-sided uplift shelf.

The accepted study configuration is:

- a `32.768 km` outer radius around the selected stage focus;
- a source-derived low-relief floor with a `6 km` sample footprint and 8
  percent retained relief;
- broad structure restored with smootherstep over `1-24 km` radial distance;
- source detail restored over `5-30 km` radial distance;
- a `500 m` focus height;
- a `100-1000 m` orbit, defaulting to `400 m`;
- unrestricted yaw and a `0-30` degree elevation envelope.

The review evidence is under
`outputs/terrain/radial-backdrop-expanded-v2/`. Preserve
`outputs/terrain/radial-backdrop-expanded-v1/` as the large-core control.

## What Is Accepted

The radial envelope, expanded domain, and camera composition are accepted as a
macro target. Scene views show no visible circular shelf, the foreground stays
clear, and every tested seed and yaw retains useful terrain. Starting broad
restoration at `1 km` reduces the dead center without pulling relief into the
maximum camera orbit because the 23 km smootherstep band begins with zero
slope and grows slowly.

The exact circular gates remain visible in top diagnostics. That is an explicit
composition compromise, not terrain truth. It is acceptable only for the
fixed-focus far-field product while it stays hidden in supported scene views.

## What Is Not Accepted

The study does not promote:

- the full-stride study mesh, which has already missed the `<1 ms` GPU target;
- `mountains-hierarchy-v2` as a final terrain source or close-view model;
- current smooth faces, sparse secondary ridges, or low material bandwidth as
  sufficient terrain detail;
- a traversable center, surface camera, foliage, water, hydrology, translation,
  streaming, or planet-scale terrain;
- radial attenuation as a general procedural-terrain contract.

The current cached hard-cut backdrop remains production v1 until the radial
composition passes through that product architecture and meets its acceptance
gates.

The first cached integration result is recorded in
[`terrain-cached-radial-integration.md`](terrain-cached-radial-integration.md).
Stride 2 and stride 3 preserve this macro baseline, but measure `1.524 ms` and
`1.338 ms` p95 respectively and are not promoted. The next bounded test removes
the continuous diagnostic center from runtime ownership while leaving this
composition unchanged.

## Cached Integration Target

Build the next lane through the existing cached backdrop ownership model:

1. Wrap the selected height source with radial relief during setup only.
2. Bake the resulting field into cached positions, geometry normals, material
   classification, ambient visibility, sectors, and reduced far-field indices.
3. Cover the `32.768 km` domain with logarithmic radial spacing and bounded
   submitted geometry; do not evaluate procedural source or radial composition
   per frame.
4. Preserve unrestricted yaw and validate the `100-1000 m` orbit around the
   elevated focus.
5. Keep the foreground consumer-owned. Terrain need not provide close ground
   merely because the composition has a continuous diagnostic source.

The cached integration must remain below `1.0 ms` p95 for `terrain surface` at
2560 x 1440 after 30 warmup frames and at least 120 measured samples. Record
setup time, peak memory, triangle submission, sector culling, and the same
three-seed/six-yaw visual matrix. Production defaults do not change on a visual
pass alone.

## Detail Follow-up

Detail work begins only after a cached integration preserves the macro baseline
and performance gate. Keep two concerns separate:

- Source/geometry detail: add filtered secondary ridges, face breakup, and
  silhouette bandwidth without changing the radial envelope or turning the
  mountains into high-frequency noise.
- Rendering detail: add procedural albedo, roughness, normals, and restrained
  atmospheric contrast with distance/footprint filtering. Material detail must
  not disguise weak geometry or introduce shimmer.

Review close debug views to expose defects, but accept terrain v1 against its
far-field backdrop distance. A later mid-field or traversable terrain product
needs a separate source, LOD, material, and content contract.

## Stop Condition

Do not continue tuning the radial center or transition while integration and
detail remain untouched. Reopen macro composition only if the cached renderer
reveals a visible ring, foreground intersection, empty yaw sector, or framing
failure that is absent from the accepted v2 evidence.
