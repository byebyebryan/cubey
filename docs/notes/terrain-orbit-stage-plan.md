# Terrain Orbit Stage Plan

Date: 2026-07-15

Status: implemented checkpoint.

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
beyond it. The orbit radius is 50-250 m, with a 100 m default. Elevation is
0-30 degrees, with an 8-degree default. The canonical camera target is the
consumer's local origin, and the conceptual stage plane is 20 m below it.
The validation proxy renders only a 20 m sphere at the focus; it deliberately
does not render a platform or ground plane owned by the consuming scene.

Detached placement no longer uses a fixed height above the local ownership
edge. That rule technically put the focus in the air but still let the bottom
of a 40-degree frame meet terrain roughly half a kilometer away. Instead, the
planner raises the source-space focus until the first terrain intersection
through the lower frame is at least 1.5 km away throughout the supported orbit
envelope. This is a composition contract rather than a source-shaping rule.

`grounded` is a supported diagnostic. Terrain stays continuous through the
focus and placement searches for a naturally low-slope patch. The orbit radius
is the same 50-250 m envelope. Elevation is 12-32 degrees, with a 20-degree
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
plan must provide at least 250 m of relief in the 3.2-6.6 km band in at least 14
sectors. The lower-frustum contract evaluates 24 yaw sectors, the
minimum/default/maximum radius and elevation values, and five rays across the
bottom edge. No tested ray may meet terrain inside 1.5 km. One selected azimuth
supplies the default still; it is not a yaw constraint.

## Runtime Boundary

The plan publishes the selected source-space focus, physical target height,
local terrain offset, orbit limits, default azimuth, lower-frustum distance,
panorama measurements, and contract status. Backdrop cameras orbit local
`{0,0,0}`. The renderer samples terrain, weathering, procedural material fields,
and heightfield shadows at `local_xz + source_focus_xz`, then renders height as
`source_height - target_height`. Geometry and the detached ownership zone stay
in local scene coordinates. Atmosphere transport receives the corresponding
physical camera altitude separately. CPU source equations, GPU source
equations, source hashes, and raw point-query semantics remain unchanged.

The detached cutout applies to rasterized terrain ownership. The local
heightfield-shadow approximation retains the continuous translated source field;
discarding individual horizon taps inside the cutout produced visible sample
rings, while no separate terrain shadow geometry is exported to consumers.

Quality stage views use the same projected-edge adaptive tessellation as other
quality cameras. The quality renderer now uses adjacent world-aligned tiles
rather than overlapping clipmap levels, so there is no parent/child silhouette
boundary to conceal with stage-specific fixed factors. The control renderer
retains its existing clipmap topology and ownership rules.

`backdrop` remains the clean orbit product view. `backdrop-stage` is a dedicated
validation view with a neutral sphere at the same local focus and the resolved
placement metrics in the GUI. It validates depth, scale, lighting, and
full-orbit composition without coupling terrain to a consuming project.
`midground` remains the existing directional surface diagnostic.

## Acceptance

- Detached plans pass for mountain v2.1 seeds `0`, `9012`, and `12345`.
- Yaw remains unrestricted through a complete interactive and headless orbit.
- Radius and elevation cannot leave the validated envelope.
- The lower edge of every sampled detached frame remains terrain-free for at
  least 1.5 km.
- The clean backdrop and proxy validation views use the same placement and
  camera transform.
- Grounded plans are finite and deterministic whether or not they pass.
- Source values and source-summary hashes remain unchanged.
- Review evidence includes paired clean/proxy views at eight azimuths per seed,
  ownership-envelope diagnostics, and a full-orbit temporal check.
- Quality geometry remains continuous through unrestricted yaw at every tested
  radius and elevation without exposing coarse parent slabs.

External scene integration follows after this project-local placement and
camera contract is accepted. Detached mode is the far-field product; grounded
mode does not claim close-range material or vegetation quality.

## Evidence

`terrain_backdrop_stage_report` publishes deterministic detached and grounded
plans for mountain v2.1 seeds `0`, `9012`, and `12345`. The corrected review pack
is generated by `projects/terrain/capture_midair_stage_review.sh` under
`outputs/terrain/midair-stage-v1/`. It contains paired clean/proxy views at eight
azimuths per seed, maximum-radius/elevation ownership-envelope frames, planner
JSON, and a duration-normalized full-orbit proxy video. Canonical frames use a
30-degree sun. The earlier fixed-height evidence remains under
`outputs/terrain/orbit-stage-v1/` as the before-state.

The quality tile-field correction is generated by
`projects/terrain/capture_quality_tile_review.sh` under
`outputs/terrain/quality-tile-v1/`. It adds six-direction clean/proxy pairs,
the complete radius/elevation envelope, native-resolution multi-seed frames,
tessellation and ownership diagnostics, a full-orbit video, and measured frame
timing. `docs/notes/terrain-quality-tile-field.md` records the renderer decision
and its intentionally finite scope.
