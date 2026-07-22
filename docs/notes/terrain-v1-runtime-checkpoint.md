# Terrain V1 Runtime Checkpoint

Date: 2026-07-11

Status: superseded historical checkpoint.

This note describes the removed directly sampled clipmap runtime. It is retained
for provenance and is not current terrain direction. The active product is the
external-raster fixed-focus backdrop documented in
[Terrain V1 Runtime](../architecture/terrain-v1.md).

## What Landed

The active `projects/terrain` project is now a standalone traversable terrain
runtime rather than a regional landscape generator. One generic evaluator
combines macro, structure, and detail bands for the `mountain`, `upland`, and
`plains` parameter sets. The same resolved parameter block drives CPU point
queries and the GLSL evaluator.

The renderer samples that source directly over an eight-level camera-centered
clipmap. It includes footprint filtering, topology-aligned transition morph,
shared atmosphere and HDR composition, heightfield self-shadowing, procedural
linear-space materials, surface and ground traversal with CPU clearance
queries, and neutral source/component diagnostics. No baked heightfield is
needed for normal rendering.

The closure pass gives every visible fragment one LOD owner. Clipmap levels use
one finest-grid-snapped origin, an exact eleven-parent-cell overlap, transition
positions and source footprints toward the parent grid, and cover residual
boundary raster gaps with narrow downward skirts. The rendering refinement
pack verifies the handoff from a two-meter eye-level camera as well as the
original high surface camera.

Optional `local` weathering is a bounded finite-neighborhood detail transform.
It deliberately has no drainage, flow, sediment, or regional state. The old
analytical patch and hydrology work remains available as the separate paused
`studies/terrain/hydrology` experiment.

## Fixed Review Pack

Generate the checkpoint with:

```sh
projects/terrain/capture_v1_review.sh
```

The script replaces its prior generated PNGs under
`outputs/terrain/v1-reboot/` and writes:

- `terrain-v1-shape-sheet.png`: rows are mountain, upland, and plains; columns
  are seeds `0`, `9012`, and `12345`; clean top-view height presentation;
- `terrain-v1-presentation-sheet.png`: the same preset/seed matrix in the
  atmosphere-lit oblique view with local weathering;
- `terrain-v1-surface-sheet.png`: mountain, upland, and plains from the
  traversable camera at seed `9012`;
- `terrain-v1-weathering-sheet.png`: one row per preset with clean height,
  weathered height, and amplified signed weathering delta from the surface
  camera;
- `terrain-v1-control-sheet.png`: TerrainEngine reference control followed by
  terrain v1 mountain presentation;
- `terrain-v1-lod-sheet.png`: top, oblique, and surface ownership views for the
  mountain control;
- `terrain-v1-lod-traversal.mp4`: deterministic six-second, 1.32 km forward
  traversal in the LOD ownership view;
- `source-summary.json`: bounded clean-source measurements over a 32.768 km
  domain at `65 x 65` samples.

The source summary after the bounded tuning pass reports:

| Preset | Relief across seeds | Mean slope across seeds |
| --- | ---: | ---: |
| mountain | 1.82-2.29 km | 0.449-0.454 |
| upland | 0.607-0.689 km | 0.1269-0.1270 |
| plains | 0.160-0.174 km | 0.0167-0.0172 |

The mountain tuning moved energy out of roughly 100 m stacked octaves and into
the broader structural band. Compared with the initial runtime capture, it
raises useful macro relief while reducing measured mean slope from about `0.70`
to about `0.45`. Meter-scale shading detail is presentation-only and fades with
screen footprint; it does not alter CPU height, collision, or silhouette.

## Review Read

- All three presets remain continuous world-space fields across every reviewed
  seed. No centered masks, authored paths, quadrant layouts, or drainage grids
  are present.
- Mountain has broad buildup, local ridges, and low valleys without the thin
  fin and isolated-spike construction seen in the legacy workbench.
- Upland and plains intentionally share the source vocabulary while separating
  relief by roughly one order of magnitude from mountain to plains.
- Local weathering preserves the coarse silhouette and disappears when the
  query footprint cannot resolve its neighborhood.
- The review confirms weathering as a selective detail operator: its signed
  delta is readable on mountain slopes and intentionally approaches a no-op on
  the low-slope upland and plains controls.
- CPU/GPU parity remains an explicit test across presets, coordinates,
  footprints, and clean/weathered modes.
- The surface controller is tested over a ten-second, 2.2 km fixed-step path;
  every sampled frame retains the requested terrain clearance.

This is a credible rendering and engine-consumer terrain baseline, not a claim
of geomorphological simulation. Materials remain generic, self-shadowing sees
only the terrain heightfield, and vegetation, water bodies, persistence, planet
projection, general scene shadows, and regional hydrology remain outside v1.

The separate rendering checkpoint and its deterministic review pack are
documented in [`terrain-rendering-refinement.md`](terrain-rendering-refinement.md).

## Next Decision

Stop adding source features inside this checkpoint. Integrate one real external
consumer, preferably a scene that needs terrain as a backdrop and CPU surface
queries. Use that integration to test lifetime, coordinate, material, and LOD
contracts before promoting any project-local terrain interface into engine
foundation.

Hydrology should resume only as its own regional experiment with an explicit
quality and performance target. It must not become an implicit prerequisite for
the directly sampled terrain runtime.

## Validation

The checkpoint was validated from the synced terrain worktree with:

```sh
cmake --build --preset dev -j 8
ctest --preset dev --output-on-failure
```

The full build completed, and all `212/212` tests passed in `1058.79 s`. That
run includes the 21 terrain-labeled tests, the twelve archived hydrology-lab
tests, CPU/shared procedural parity, Vulkan terrain source parity, windowed
smoke, oblique/surface/ground component captures, both terrain videos, and the
unrelated atmosphere, cloud, ocean, planet, fluid, reference, and legacy gates.
