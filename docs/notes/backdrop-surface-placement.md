# Backdrop Surface Placement

Date: 2026-07-24

Status: implemented shared terrain and ocean foreground-clearance contract.

## Problem

Backdrop consumers previously translated terrain and ocean surfaces with open-coded
anchor-minus-height formulas. The configured foreground height described only the
distance from a scene anchor to the nominal surface. It did not account for:

- the foreground object's lower bound;
- terrain relief under the object's horizontal footprint;
- dynamic ocean crests;
- a consumer-specific clearance margin.

This allowed a locally raised terrain triangle or wave crest to clip a foreground
volume even when the nominal center sample appeared correctly placed.

## Contract

`cubey::render::resolve_backdrop_surface_placement` resolves one vertical transform
from two envelopes:

- a surface nominal height and maximum local height;
- a foreground world anchor and minimum local height.

The request retains the existing preferred foreground height and adds a minimum
clearance. The default policy raises an unsafe preferred height to the required
floor. `ExactRequestedHeight` remains available for explicit intersection studies
and reports the resulting penetration without silently correcting it.

The result publishes:

- required and effective foreground heights;
- surface world translation;
- achieved clearance;
- adjustment, satisfaction, and intersection diagnostics.

Consumers continue to own policy. Fire and Water3D use their volume bounds and a
`0.10 m` margin. The glTF viewer uses imported scene bounds and a `0.10 m` margin.
Their established default foreground heights remain preferred presentation
distances and are unchanged when already safe.

## Terrain Envelope

Raster terrain preparation now accepts a foreground footprint radius and returns a
surface envelope. The envelope is evaluated against the final triangulated terrain
product, not by independently resampling the source heightfield. Triangle vertices,
edge-circle intersections, and the planar maximum inside the footprint are included,
so placement agrees with the geometry submitted to the renderer.

This is an exact vertical bound for the baked triangles inside the requested disk.
It does not claim clearance against source detail that the current terrain mesh does
not resolve.

## Ocean Envelope

Ocean placement uses a crest allowance derived from the enabled cascades'
displacement scales and the surface-shape strength. It participates in the same
generic resolver and updates when the glTF viewer changes sea state.

The allowance is a stable design envelope, not a mathematical bound on every FFT
sample. A strict ocean-contact feature would need a GPU reduction or a separately
validated spectral bound. Backdrop placement deliberately avoids that readback cost.

## Controls And Validation

The shared terrain CLI now accepts non-negative sub-meter preferred heights. Each
consumer clamps its runtime control to the resolved clearance floor, avoiding a UI
range that promises an unsafe placement.

Validation includes:

- unit tests for safe, corrected, intersecting, and invalid generic placements;
- exact terrain triangle-footprint envelope tests;
- ocean allowance tests across enabled cascades and shape strength;
- sub-meter and negative terrain CLI tests;
- 26 focused config, headless, terrain, ocean, Water3D, Fire, Explosion, and glTF
  viewer tests;
- 1280x720 low-request review captures under `outputs/backdrop-placement-*.png`.

