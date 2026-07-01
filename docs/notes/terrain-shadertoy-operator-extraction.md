# Terrain ShaderToy Operator Extraction

Date: 2026-06-30

This note captures the deeper ShaderToy terrain/hydro review before the next
terrain implementation batch. The goal is to decide what Cubey should extract
from compact ShaderToy examples without drifting back into one-off authored
terrain masks or shader-only visuals.

## Decision

Use ShaderToy terrain and hydro examples as **operator and visual-vocabulary
references**, not as terrain architecture, river topology, or renderer code to
port.

The Cubey terrain path remains:

```text
coherent source fields -> terrain process fields -> product fields -> consumers
```

ShaderToy fits this path when it helps define a small deterministic field
operator, diagnostic output, or capture target. It does not fit when the effect
depends on a full-screen raymarcher, implicit feedback buffers, mouse/time state,
screen-space post, or an authored river path hidden inside one shader.

## Extraction Rules

- Keep all extraction clean-room. Several useful examples have noncommercial,
  share-alike, MPL, mixed, or unclear provenance, and the local comments are not
  enough to justify direct code import.
- Keep operators meter-aware. Cubey terrain uses grid cell size, height in
  meters, local relief, and named fields; ShaderToy constants in normalized UV
  space should not be copied as behavior.
- Promote an output only when it affects downstream terrain products. Temporary
  debug values can stay diagnostic, but any field that drives material,
  vegetation, routing, water, or geometry should get a name, scalar PNG, stats,
  and tests.
- Recompute dependent fields after morphology changes. If an operator modifies
  height, slope, local relief, flow, wetness, deposition, material, and
  vegetation potential must not keep describing the old surface.
- Do not turn visual review cues into product truth. Fog, color grading, water
  normals, foam, and scenic camera composition are useful review targets, not
  terrain source fields.

## Borrow Now

### Gully / Erosion Diagnostic

Primary refs:

- `~/code/ref/ShaderToy/clean_terrain_erosion_filter_common.glsl`
- `~/code/ref/ShaderToy/clean_terrain_erosion_filter_buffer_a.glsl`
- `~/code/ref/ShaderToy/advanced_terrain_erosion_filter_*`
- `~/code/ref/ShaderToy/eroded_mountains_*`

Useful extraction:

- Treat erosion as a process over height plus derivatives, not height alone.
- Tie the erosion scale to terrain scale, such as mountain width, ridge spacing,
  or local-relief radius.
- Generate explicit diagnostic fields:
  - `erosion_delta_m`
  - `gully_mask`
  - `crease_proxy`
  - optional `post_erosion_height_m` while the pass is diagnostic
- Review before/after height, delta, slope, local relief, and material response.

Why this is relevant:

The current mountain stress recipe can still read like generated bumps rather
than terrain shaped by process. A clean-room gully diagnostic gives us a way to
test whether slope-aware morphology improves ridge/valley readability before we
claim any physical erosion.

Guardrails:

- Do not copy the gully formula directly.
- Do not call the result hydraulic erosion.
- Do not feed it into rivers until the before/after field diagnostics are stable.
- Keep the first implementation terrain-local in `projects/terrain`, likely in
  `terrain_process_fields`, rather than promoting it to `cubey::procedural`.

### Shallow-Water / Lake Relaxation Diagnostic

Primary refs:

- `~/code/ref/ShaderToy/mountains_and_lakes_common.glsl`
- `~/code/ref/ShaderToy/mountains_and_lakes_buffer_a.glsl`
- `~/code/ref/ShaderToy/mountains_and_lakes_buffer_b.glsl`
- `~/code/ref/ShaderToy/mountains_and_lakes_buffer_c.glsl`
- `~/code/ref/ShaderToy/mountains_and_lakes_buffer_d.glsl`

Useful extraction:

- Fixed-iteration water-depth relaxation over `height_m + water_depth_m`.
- Four-neighbor outflow fields with total-outflow clamping.
- Explicit boundary policy and mass-change diagnostics.
- Diagnostic fields such as:
  - `water_depth_m`
  - `water_surface_m`
  - `outflow_east`, `outflow_north`, `outflow_west`, `outflow_south`
  - `lake_mask`
  - `overflow_proxy`

Why this is relevant:

Cubey will need lakes, wetlands, and standing water, but they should start as
inspectable fields over terrain products, not as full hydraulic simulation or
water rendering. The ShaderToy lake examples are compact enough to guide a first
bounded diagnostic.

Guardrails:

- This is not the next default terrain batch if mountain/erosion morphology is
  still the active priority.
- Do not reopen coast/ocean rendering inside `projects/terrain`.
- Do not rely on ShaderToy feedback-buffer lifetime or fixed 256x256 texture
  assumptions.

### Shoreline And Water-Contact Composition

Primary refs:

- `~/code/ref/ShaderToy/castaway_*`
- `~/code/ref/ShaderToy/waterworld.glsl`
- `~/code/ref/ShaderToy/misty_lake.glsl`
- `~/code/ref/ShaderToy/day_at_the_lake_*`

Useful extraction:

- Field relationships for visual review:
  - shallow/deep tint from `water_depth_m`
  - wet sand from shore distance or water/terrain height difference
  - foam bands from shoreline distance and wave/contact masks
  - seabed visibility from water depth and extinction
  - sand/rock response from slope and material masks

Why this is relevant:

These examples show how simple field combinations can make water/terrain contact
read clearly. That belongs first in debug/capture or future ocean/terrain
handoff work, not in terrain generation itself.

Guardrails:

- Do not mistake a pretty shoreline material for hydrology.
- Keep animated water normals, Fresnel, refraction, reflection, foam drift, and
  extinction in renderer or water projects when they graduate.

## Do Not Borrow For Terrain Topology

### Authored ShaderToy River Paths

`~/code/ref/ShaderToy/where_the_river_goes.glsl` is useful for water flow,
foam, and river-scene visual targets. It is not a good donor for river topology:
the riverbed is effectively an authored/meandered path embedded in the shader.

Cubey should keep river topology on the reference-backed graph/hydrology path:

- `~/code/ref/terrain-erosion-3-ways/river_network.py` for upstream/downstream
  graph logic, directional inertia, upstream volume, and capped downcutting;
- `~/code/ref/SimpleHydrology/source/water.h` and `world.h` for discharge,
  persistent momentum, channel memory, erosion/deposition deltas, and droplet
  process-state diagnostics;
- `~/code/ref/terrain-diffusion/terrain_diffusion/inference/postprocessing.py`
  only as a small clean-room reference for postprocessing ideas such as flow
  accumulation and bounded priority-flood fill.

ShaderToy should inform how river and water fields are reviewed visually, while
the graph/hydrology references should inform how river source topology and
process fields are generated.

## Fit In Current Cubey Terrain

The first implementation home is `projects/terrain`, not shared procedural
foundation. The current helper layer already owns terrain-local process helpers:

- `spread_max_decay_field`
- `clamp_split_lowering_to_relief`
- `subtract_lowering_from_height`

ShaderToy-inspired operators should extend that layer only when they have a
clear field contract. Likely insertion points:

- source/landform shaping after coherent source fields exist and before final
  height assembly;
- river/process morphology inside or adjacent to the current river-carving
  stage;
- post-carve process/material analysis after height, slope, and local relief are
  recomputed.

Existing fields already provide useful inputs:

- source/mountain: `base_elevation`, `broad_relief`, `mountain_envelope`,
  `mountain_support`, `mountain_ridge_influence`, `mountain_peak_prominence`;
- river/process: `drainage_potential`, `flow_direction`, `flow_accumulation`,
  `stream_order`, `river_mask`, `river_trunk`, `tributaries`,
  `river_graph_discharge`, `channel_width`, `valley_width`,
  `channel_incision`, `valley_incision`, `wetness`, `deposition`;
- analysis: `height_m`, `pre_process_height_m`, `slope`, `curvature`,
  `local_relief`.

## Suggested Next Terrain Process Batch

Start with erosion/gully diagnostics before adding lakes or shoreline work:

1. Add a terrain-local clean-room morphology helper that consumes height, slope
   or derivatives, local relief, and an optional support mask.
2. Emit `erosion_delta_m`, `gully_mask`, and `crease_proxy` as diagnostic fields
   for the mountain stress recipe.
3. Keep the pass opt-in or stress-only until the field reads correctly.
4. Recompute slope/local relief and export before/after views.
5. Add tests for determinism, finite values, non-negative/limited deltas, scale
   sensitivity, and no hidden mutation of the source height.
6. Compare `mountain-relief.png`, `height.png`, `slope.png`, and the perspective
   mountain captures before deciding whether the operator should affect final
   `height_m`.

Only after that should the terrain workbench try the lake/shallow-water
relaxation diagnostic. Shoreline composition should wait until the terrain/ocean
handoff is active again or until a water-body review needs visual cues.

## Success Criteria

- The output looks less like random bump detail and more like process-shaped
  terrain in both scalar and perspective views.
- The operator exposes enough intermediate fields to diagnose artifacts without
  staring only at `final.png`.
- The implementation does not require authored lines, disks, local camera
  composition, ShaderToy feedback buffers, or direct shader code reuse.
- The result remains compatible with the broader river/hydrology path instead of
  replacing it with a visual shortcut.
