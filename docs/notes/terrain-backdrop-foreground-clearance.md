# Terrain Backdrop Foreground Clearance

Date: 2026-07-12

Status: completed foreground-clearance checkpoint. This corrects the completed
backdrop presentation checkpoint without changing terrain source or material
generation.

## Problem

The first backdrop planner uses a fixed 120 m terrain clearance, a 40-degree
vertical field of view, and a minimum pitch of -2 degrees. On level terrain,
the lower-center view ray therefore reaches the ground at only about 297 m.
That is the intended 300 m presentation boundary with no safety margin. An
uphill foreground or weathered local rise can pull unsupported terrain closer
and make the backdrop read like a surface view where vegetation geometry and
fine ground detail would reasonably be expected.

The planner stores a complete transform, but the runtime currently transfers
only its anchor, yaw, and pitch into the surface controller. Camera height is
then reconstructed from the fixed preset clearance. Raising only the stored
transform would therefore have no runtime effect.

## Contract

The backdrop reset pose must satisfy both conditions:

- at least 150 m above final visible terrain at its anchor;
- at least 10 m of terrain-to-ray margin along the lower-center and lower-corner
  frustum rays for the first 300 m.

Frustum validation uses the requested capture aspect ratio, the existing
40-degree vertical field of view, and the conservative -2-degree minimum camera
pitch. It samples final terrain every 25 m from 25 through 300 m. Final terrain
includes local weathering because that is the geometry users see.

For each candidate, camera height starts at the 150 m floor and rises to meet
the worst sampled ray requirement. The final target pitch is resolved only
after that height is known. The existing near-obstruction heuristic becomes a
clearance-efficiency score: candidates that naturally meet the contract rank
above otherwise similar candidates requiring a large raise.

## Runtime Boundary

`TerrainBackdropCameraPlan` owns the selected clearance and foreground
diagnostics. The terrain app must use that planned clearance when reconstructing
the camera transform. Interactive traversal remains enabled and preserves the
selected clearance above local terrain instead of falling back to the minimum.

The 300 m frustum guarantee applies to the deterministic reset pose. A user can
still rotate or traverse away from that composition, but the camera remains at
the selected backdrop-scale altitude. The ordinary surface and ground cameras
remain explicit close-range diagnostics.

## Frozen Boundary

This pass does not change terrain source parameters, height formulas,
weathering, CPU/GPU query behavior, materials, coverage fields, clipmap layout,
or non-backdrop cameras. The source-summary SHA-256 must remain
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.

## Acceptance

- All tested plans are deterministic, finite, and at least 150 m AGL.
- Independent lower-frustum checks retain at least 10 m margin through 300 m.
- The contract holds across mountain, upland, and plains presets, multiple
  seeds, and representative 4:3, 16:9, and 21:9 aspect ratios.
- Interactive backdrop traversal preserves the selected planned clearance.
- Review captures stay useful rather than becoming unnecessarily aerial.
- The terrain source hash and standard rendering controls remain unchanged.

## Outcome

The planner now ranks all 600 anchor/heading combinations with the existing
composition fields, then evaluates strict final-terrain clearance for the top
16 candidates. This keeps the camera search deterministic while avoiding a
full terrain-query fan for every candidate. The previous approximate
near-obstruction term is replaced by a bounded clearance-efficiency term, so
naturally open foregrounds win when their composition scores are comparable.

`TerrainBackdropCameraPlan` now publishes selected AGL, additional raise,
guaranteed clear distance, minimum sampled margin, and planning aspect ratio.
The terrain app passes its initial aspect ratio into the planner and uses the
selected AGL for reset and traversal. It no longer discards the planned camera
height by rebuilding the pose at a fixed preset clearance.

Across the canonical three-preset, three-seed matrix, selected clearance ranges
from 150.0 to 200.54 m. Additional raise ranges from 0.0 to 50.54 m, and the
minimum reported lower-frustum margin ranges from 10.0 to 112.24 m. Mountain
targets remain at 1600 or 3200 m. Upland and plains retain useful low-horizon
frames rather than becoming aerial views.

The independent stress test covers six seeds per preset, including the maximum
64-bit seed, and 4:3, 16:9, and 21:9 aspect ratios. It reconstructs the lower
frustum separately from the planner and verifies the 10 m margin through 300 m.
Traversal testing verifies that the selected AGL remains authoritative after
movement.

The regenerated v2 review pack remains under
`outputs/terrain/backdrop-presentation/`. Its matrix, distance controls,
comparison sheet, showcase, and traversal video show no framing regression,
new LOD band, or coverage instability. Video-frame submission averages about
0.58 ms in the pack profile; the terrain material and per-frame render path are
unchanged.

The source-summary SHA-256 remains
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.
