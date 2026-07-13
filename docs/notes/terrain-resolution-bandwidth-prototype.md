# Terrain Resolution And Bandwidth Prototype

Date: 2026-07-12

Status: implementation contract. The current terrain renderer and source v1
remain the default control while an opt-in mountain quality path tests adaptive
geometry, a broader authoritative height spectrum, and procedural material
textures.

## Evidence

The canonical 1920 x 1080 backdrop places its mountain target roughly 3.2 km
from the camera. The fixed 128-cell clipmap resolves that area with 32-64 m
cells, or roughly 15-30 pixels per mesh edge. Halving the configured near-cell
size changes coverage placement but does not materially increase projected
density.

Source v1's shortest mountain detail octave is approximately 108 m. Its 18 m
weathering filter is disabled once the vertex footprint reaches 13.5 m, so it
does not contribute to the main backdrop rings. The current material evaluator
adds smooth fields at 680, 145, and 34 m but no sub-meter or few-meter surface
bandwidth.

TerrainEngine's visual advantage comes from a height spectrum extending to a
few meters, distance-adaptive tessellation, high-resolution diffuse and normal
textures, and full-scene composition. Its exact LOD ladder and imported assets
are not the target. Cubey will test the same capabilities through reusable
Vulkan support and generated procedural data.

## Configuration

- `terrain.render_path=control|quality`, default `control`;
- `terrain.source_version=v1|v2`, default `v1`;
- `terrain.target_edge_px`, default `4`, valid range `2-16`.

Control rendering and source v1 remain behaviorally and byte-for-byte stable.
Quality rendering may be paired with source v1 to isolate rendering changes or
source v2 to evaluate the integrated result. This prototype supports quality
rendering and source v2 for the mountain preset only.

Quality rendering requires Vulkan tessellation support and fails with a clear
capability error when explicitly requested on unsupported hardware. Control
rendering remains available without tessellation.

## Source V2

Mountain v2 retains v1 macro, structure, elevation, and weathering parameters.
Its detail band uses eight octaves from 900 m to approximately 6 m, lacunarity
2.03, gain 0.52, ridge mix 0.24, and composition weight 0.16. Footprint
filtering remains authoritative in CPU and GPU sampling.

The v1 report and expected SHA-256 remain unchanged. V2 receives a separate
report hash and parity matrix at 0, 2, 8, 32, and 64 m footprints.

## Quality Geometry

The quality path covers the same eight levels and 16,384 m half-extent with
coarse 16-cell quad patches. Tessellation factors derive from projected edge
size, quantize to powers of two from 1 through 64, and target four pixels per
edge. Shared edge calculations, conservative patch culling, clipmap ownership,
and generated-vertex footprints must remain deterministic and crack-free.

## Quality Materials

Ground, scree, rock, and snow each receive a seeded 1024 x 1024 RGBA8
procedural tile with a 256 m period and complete mip chain. Channels encode
normal XY, albedo variation, and roughness. Periodic procedural features span
64 m to 0.25 m and are sampled through warped world-space triplanar projection.
No imported runtime texture is permitted.

## Acceptance

- control/v1 remains the default and preserves the existing source hash;
- quality projected edges remain at or below six pixels in the canonical
  backdrop, including the tessellation-factor cap;
- v2 retains visible 8-16 m mountain structure without LOD cracks or shimmer;
- quality material high-frequency energy is at least twice the control crop;
- CPU/GPU source v2 parity passes at all representative footprints;
- incremental 960 x 540 capture cost remains below 33.3 ms/frame;
- fixed-camera control/v1, quality/v1, and quality/v2 artifacts make each
  contribution independently reviewable.

Upland, plains, water, foliage geometry, hydrology, and final cloud/water scene
composition remain outside this prototype.
