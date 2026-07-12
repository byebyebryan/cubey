# Terrain Backdrop Presentation Study

Date: 2026-07-12

Status: completed presentation checkpoint. Terrain v1 source generation stayed
frozen; the pass changed only camera composition and opt-in material response.

## Problem

Terrain v1 reads clearly at aerial and macro scale, but its generic green
surface implies grass that does not exist. At ground level that mismatch makes
the scene feel sparse and exposes a level of vegetation detail that the current
heightfield renderer does not provide. The immediate product need is narrower:
terrain should work as a coherent background for rendering projects without
claiming to be a close-range biome or foliage system.

## Reference Read

TerrainEngine is a useful visual control, but it does not render procedural
grass geometry. `DrawableObjects/Terrain.cpp` loads sand, grass, rock, snow,
rock-normal, and terrain textures. Its terrain fragment shader blends two grass
textures with a two-octave noise coefficient, then gates the material by slope
and elevation. The README still lists procedural grass as future work. Its
strongest screenshots use elevated framing or water as foreground separation.

Selected local ShaderToy references demonstrate a range of increasingly costly
illusions:

- `terrain_buffer_a.glsl` uses elevation and surface orientation to tint likely
  tree regions, without geometry;
- `dry_rocky_gorge.glsl` tints flat surfaces as dry vegetation and explicitly
  describes the result as a hint rather than physical trees;
- `mountains.glsl` adds dark noisy tree coverage and a small visual height to
  strengthen distant silhouettes;
- `windy_plains.glsl` raymarches a roughly 20 cm noisy grass canopy;
- `rainforest_buffer_a.glsl` raymarches distorted ellipsoid tree envelopes and
  notes that the technique works mainly at distance and low resolution.

The first three establish the useful v1 pattern: broad coverage derived from
terrain fields, then multiscale procedural breakup. The latter two establish
the boundary where real geometric or raymarched vegetation becomes necessary.

The terrain and mountain examples are marked CC BY-NC-SA. The rainforest work
is shared for educational use under more restrictive terms. Cubey borrows only
the general observations above and does not copy code, constants, or source
structure from those shaders.

## Frozen Boundary

The source parameters, height equations, local weathering transform, CPU/GPU
query contract, and source report must remain byte-identical. The expected
source-summary SHA-256 is
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.

This study does not add grass blades, tree geometry, displacement, collision,
vegetation shadows, imported textures, hydrology, water, or a biome simulator.
Presentation fields cannot feed terrain height or source queries.

## Direction

Add a deterministic `backdrop` camera that searches the directly sampled
terrain for a useful near field, distant prominence, and varied silhouette.
The same seed and preset must always produce the same frame. Interactive use
may traverse from that pose; headless still captures remain static.

Add an opt-in `backdrop` presentation mode. It derives separate low vegetation
and woody coverage from physical elevation, source slope, broad landform, and
kilometer-to-tens-of-meters coherent variation. Coverage changes only color,
roughness, and restrained material normals. The default `standard` mode remains
the visual regression control.

## Distance Contract

Backdrop presentation is intended for scene terrain whose visible lower edge
begins at about 300 m or farther on level ground. It may suggest meadow and
woodland masses at that range; it does not claim credible individual plants.
The ordinary `surface` camera checks intermediate use. The two-meter `ground`
camera is a required negative control and is expected to reveal the missing
close vegetation.

## Acceptance

- All three source presets and seeds `0`, `9012`, and `12345` receive finite,
  useful, deterministic backdrop framing.
- The backdrop frame favors a clear near field and a prominent, laterally
  varied distant silhouette rather than a random compass direction.
- Coverage forms broad coherent meadow and woodland masses while leaving
  geology visible and avoiding high-frequency green noise.
- Standard and clay paths remain unchanged controls.
- Moving surface review exposes no coverage shimmer or LOD bands.
- The ground negative control is documented as unsupported rather than tuned
  until it hides the missing geometry.
- Capture-body cost remains below the existing `33.3 ms/frame` evaluation
  budget.

## Outcome

Terrain now has a deterministic `backdrop` camera. It evaluates a fixed `5 x 5`
anchor grid and 24 headings against clean source samples at 400, 800, 1600,
3200, and 6400 m. The score favors distant prominence, lateral silhouette
variation, useful target distance, and a clear near field in that order. The
original winning pose used 120 m final-source clearance and a 40-degree vertical
field of view. The later foreground-clearance checkpoint replaced that fixed
value with a 150 m floor plus final-terrain frustum validation. All nine
preset/seed review cases select useful frames; target
distances are 1600 or 3200 m. Headless backdrop captures remain static, while
the same pose seeds the interactive surface controller.

The opt-in `terrain.presentation=backdrop` path adds separate ground and woody
coverage. A 96 m clean-source neighborhood supplies broad landform context;
rotated kilometer, 155 m, and 31 m procedural fields supply regional, patch,
and clump variation. Projected footprint filtering removes unresolved breakup.
Coverage changes only base color, roughness, and restrained material relief,
with total color influence capped at 75%. It does not affect terrain height,
queries, collision, shadows, or source diagnostics. `vegetation-coverage`
publishes the two fields as red and green channels.

The matrix shows coherent distant meadow and dark woodland masses across all
three presets. Mountains receive prominent relief framing; lower-relief upland
and plains cases correctly use long horizons rather than inventing peaks. The
six-second moving surface capture shows no visible coverage seams or LOD bands.
The two-meter ground control still reads as a smooth colored heightfield and is
not supported as foliage presentation. The ordinary 70 m surface view is an
intermediate diagnostic, not the backdrop contract.

Generate the accepted pack with:

```sh
projects/terrain/capture_backdrop_review.sh
```

It replaces `outputs/terrain/backdrop-presentation/` with:

- `terrain-backdrop-matrix-sheet.png`: three presets by three seeds;
- `terrain-backdrop-comparison-sheet.png`: standard, backdrop, and coverage;
- `terrain-backdrop-distance-sheet.png`: ground, surface, and backdrop controls;
- `terrain-backdrop-showcase-1920x1080.png`: full-resolution presentation frame;
- `terrain-backdrop-surface-traversal.mp4`: six-second temporal/LOD review;
- camera-plan JSON, source summary, profile records, and review metadata.

The source-summary SHA-256 remains
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.
On the local RTX 5070 Ti, 60 and 300 frame `960 x 540` video captures took
`8.24 s` and `9.85 s`. Their incremental difference is approximately
`6.71 ms/frame`, including capture submission, readback, and encoding, below
the `33.3 ms/frame` evaluation budget. The profile reports video-frame
submission itself at about `0.62 ms/frame`; startup remains a separate cost.
