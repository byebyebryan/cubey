# ShaderToy Terrain Source-Shape Studies

Date: 2026-07-15

## Decision

The direct Mountains transfer reads more convincingly than Cubey terrain v2.1
even through a coarse mesh and incomplete presentation. Add a bounded comparison
lane to determine whether that advantage comes from its alternating signed
octaves specifically or from a broader class of coherent height constructions.

Extend the optional `terrain_shadertoy_ref` application with four study modes:

- `mountains`: the existing David Hoskins control;
- `swiss-alps`: derivative-damped global fBm with broad folded uplift;
- `mountain-peak`: a derivative-warped multifractal with explicit radial focus;
- `erosion-filter`: the external advanced erosion filter applied to the
  Mountains base field.

The last mode is a process study, not an Eroded Mountains source port. The local
Eroded Mountains demo initializes its terrain from a hand-painted center bump,
which violates Cubey's coherent-source requirement and is not a useful global
mountain baseline.

## Boundary

All source files remain under `~/code/ref/ShaderToy` and are included only by
the optional local target. Cubey must not vendor the source or generated SPIR-V.
Mountains and Mountain Peak declare CC BY-NC-SA 3.0. The Swiss Alps bundle does
not declare a reusable license in the archived files and remains audit-only.
The Phacelle noise and Advanced Terrain Erosion Filter sections declare
MPL-2.0, but the surrounding reference bundle remains external.

Only the height construction is compared. Non-Mountains studies use the shared
mesh, orbit camera, clay shading, height diagnostic, and slope diagnostic.
Their original raymarching, materials, clouds, water, temporal reconstruction,
and post-processing are out of scope. Missing any required external source
disables the optional target without affecting normal Cubey builds.

## Comparison Contract

`--reference-study` selects `mountains`, `swiss-alps`, `mountain-peak`, or
`erosion-filter`. Mountains retains its existing defaults and exact raymarch
control. Other studies default to mesh rendering, atlas normals, terrain-only
geometry, and clay shading; unsupported requests for the Mountains raymarch,
original material, or exact detailed normals fail explicitly.

Each adapter writes the existing atlas contract: broad geometry height in `R`,
surface height in `G`, and the highest available normal-detail height in `B`.
Swiss Alps uses its medium- and high-octave terrain functions. Mountain Peak
uses its geometry and fragment octave counts while retaining the reference's
radial attenuation as an audit warning. The erosion lane first normalizes and
bakes Mountains height plus slope, then evaluates the external filter into a
second atlas.

The review pack contains fixed-yaw clay views and top height/slope diagnostics
for every study. The goal is comparative morphology: broad mass, shoulders,
peak buildup, ridge width, and obvious masks or seams. This batch does not
promote a source, modify `projects/terrain`, tune the production renderer, or
set a runtime budget.
