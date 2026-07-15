# Terrain Orbit Stage Plan

Date: 2026-07-14

Status: implementation contract.

## Correction

The first far-field checkpoint treated the backdrop as a traversable surface
camera inside a 200 m movement zone and a 30-degree yaw cone. That contract can
produce a useful staged frame, but it is not suitable for a reusable scene
backdrop. Cubey's expected consumer is normally an orbit camera around a fixed
foreground focus. Yaw therefore needs to remain unrestricted, while placement,
orbit radius, and orbit elevation form the supported envelope.

The terrain source remains an unbounded random-access field. Placement selects
a deterministic source location and maps it to the consumer's local origin. It
does not move the consumer to a large world coordinate and does not flatten,
mask, or author a feature into the source.

## Modes

`detached` is the supported far-field v1 product. The consumer owns the inner
300 m around the focus. Terrain rendering excludes that local zone and starts
beyond it, while the camera and review contract keep the boundary below the
final frame or behind consumer geometry. The orbit radius is 50-150 m, with a
100 m default. Elevation is 4-12 degrees, with an 8-degree default. The
foreground stage sits 150 m above the highest clean terrain sample inside a
400 m guard radius, and the canonical camera target is 20 m above that stage.

`grounded` is a supported diagnostic. Terrain stays continuous through the
focus and placement searches for a naturally low-slope patch. The orbit radius
is the same 50-150 m envelope. Elevation is 12-32 degrees, with a 20-degree
default, and the camera target is 20 m above the selected surface. A candidate
passes when the 300 m stage stays within 40 m of relief, its sampled p95 slope
does not exceed 0.15, and every representative orbit camera retains 10 m of
terrain clearance. A failed search returns the deterministic best candidate
with a failed-contract flag. It never changes preset or source shape.

## Placement

The planner searches a bounded but source-independent region:

1. score a 17 x 17 grid over plus or minus 32 km at 4 km spacing;
2. refine the best 24 candidates with 1 km neighborhoods;
3. refine the best eight candidates with 250 m neighborhoods;
4. perform the full orbit and panorama evaluation on the best 16 candidates.

Stable coordinate ordering breaks equal scores. The cheap stages use
footprint-filtered height and slope samples; only the final shortlist pays for
frustum and horizon tests. The setup path remains single-threaded in this
checkpoint. The six-plan three-seed report should stay within twice the current
8.1 second directional-report baseline before planner parallelism is
considered.

The panoramic contract samples 24 azimuth sectors. Every supported detached
plan must keep frame-center terrain out to 2.4 km and provide at least 250 m of
relief in the 3.2-6.6 km band in at least 14 sectors. The camera envelope is
checked at eight azimuths, both radius limits, and both elevation limits. One
selected azimuth supplies the default still; it is not a yaw constraint.

## Runtime Boundary

The plan publishes the selected source-space focus, local stage height, orbit
limits, default azimuth, panorama measurements, and contract status. The
renderer samples terrain, weathering, procedural material fields, and
heightfield shadows at `local_xz + source_focus_xz`, while geometry and the
detached ownership zone stay in local scene coordinates. CPU source equations,
GPU source equations, source hashes, and raw point-query semantics remain
unchanged.

`backdrop` becomes an orbit camera. `midground` remains the existing
directional surface diagnostic. The shared orbit controller may gain
configurable pitch limits, but its defaults must preserve all existing
consumers.

## Acceptance

- Detached plans pass for mountain v2.1 seeds `0`, `9012`, and `12345`.
- Yaw remains unrestricted through a complete interactive and headless orbit.
- Radius and elevation cannot leave the validated envelope.
- Final detached frames never expose the ownership boundary.
- Grounded plans are finite and deterministic whether or not they pass.
- Source values and source-summary hashes remain unchanged.
- Review evidence includes eight detached azimuths per seed, grounded
  diagnostics, ownership diagnostics, and a full-orbit temporal check.

External scene integration follows after this project-local placement and
camera contract is accepted. Detached mode is the far-field product; grounded
mode does not claim close-range material or vegetation quality.
