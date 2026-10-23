# SDF material workshop implementation contract

Date: 2026-10-23. Status: authorized alongside the PBR material workshop.

Stage B uses an explicit workshop mode in the existing SDF executable, retaining
one renderer, field implementation and accepted scene path. The shared recipe
library is the source of material values. Inline v2 authored scenes remain valid.

Checkpoints:

1. Mechanically promote CPU and GLSL surface evaluation; verify the accepted
   catalog source hashes, old scene fingerprints and native control captures.
2. Load a shared recipe into an explicit workshop specimen with identity
   material coordinates. Sphere radius 0.25 and beveled block match the mesh
   fixtures. A small CSG specimen combines smooth fusion, groove and through-cut.
3. Retain the existing SDF material UI and add recipe selection, reset, save/load,
   pattern/bump toggles, and controls for cut ownership and smooth material blend.
   Appearance changes do not rebuild the field; geometry changes do.
4. Reuse fixed studio, clear low sun and frozen cloud settings. Qualify opaque
   sampling/coordinate agreement, geometric versus bump normals, absorption and
   existing bounded glass interface tracing. Mesh thickness is an estimate;
   SDF glass measures its in-medium segment lengths.
5. Capture native matched views and a focused CSG inspection sequence, profile
   steady rendering separately from setup/rebuild, publish a small report and
   record evidence. Preserve guardian, robot, legacy inline scenes and history.

No new sculpting tools, authored showcase expansion, material graph, caustics,
transport solver or material realism iteration is included. Commit locally.
