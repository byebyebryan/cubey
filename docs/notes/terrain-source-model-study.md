# Terrain Source Model Study

Date: 2026-07-15

## Decision

Run the next mountain-source comparison inside `projects/terrain`, through the
accepted cached-backdrop renderer. Keep `studies/terrain/reference` frozen: it is a
visual benchmark and provenance record, not a library or an active recipe lane.

The study isolates source shape from the two other known weaknesses in the
current backdrop:

- production renders every third sample from the high-density cached product,
  which can make peaks visibly low-poly;
- the current v2.1 source independently sums broad, structure, and detail fBm
  before a strong elevation power, which tends to form rounded noise masses
  instead of a gradual range-to-ridge-to-summit hierarchy.

Source candidates therefore use the same full cached topology, stage planner,
camera, clay/material views, atmosphere, and lighting. The study does not tune
the renderer per candidate and does not promote a winner automatically.

## Comparison Contract

The fixed comparison uses:

- recipes `control-v2-1`, `terrain-engine-fbm`, `elevated-derivative`,
  `swiss-derivative`, `mountains-signed`, `rainforest-cliff`, and
  `mountain-peak-warp`;
- seeds `0`, `9012`, and `12345`;
- a common `14 km` base period and deterministic named seed streams for new
  candidates;
- footprint-aware octave filtering for every candidate;
- no hydraulic/local weathering, water, foliage, focal masks, authored lines,
  candidate-specific materials, or candidate-specific camera placement;
- the production `3.2-16.384 km` backdrop extent, but full render topology for
  offline source review rather than the production stride-three topology;
- a fixed canonical top-view domain plus the normal deterministic stage search
  for presentation views.

Different operators do not naturally produce comparable units. For the study,
each recipe is calibrated once over all three fixed seeds on a `257x257` grid
covering `[-16.384, 16.384] km` in both axes. Aggregate raw `p05` maps to `0 m`
and raw `p95` maps to `3500 m`. Values below zero are floored; high values are
not clamped. Raw distributions remain in the report so calibration cannot hide
an unstable or extreme source.

## Reference Matrix

All implementations are clean-room operator studies. Do not copy shader code,
hashes, constants, textures, scene composition, or rendering code from these
references.

| Study recipe | Local reference | Concept under test | Deliberate deviation |
| --- | --- | --- | --- |
| `terrain-engine-fbm` | `~/code/ref/TerrainEngine-OpenGL` | compact value-fBm plus nonlinear uplift | shared Cubey noise, world-meter scale, no reference material/tessellation |
| `elevated-derivative` | Elevated source archive and local research notes | accumulated derivative damping across octaves | independent value-noise derivative and calibration |
| `swiss-derivative` | `ShaderToy/swiss_alps_buffer_b.glsl` | derivative-damped fBm with broad mountain remap | no texture inputs, renderer, or temporal reprojection |
| `mountains-signed` | `ShaderToy/mountains.glsl` | rotated signed octave coupling and broad uplift | no trees, raymarching, or copied hash/weight sequence |
| `rainforest-cliff` | `ShaderToy/rainforest_*` | ordinary broad fBm followed by bounded cliff emphasis | no SDF scene, foliage, or copied restricted source |
| `mountain-peak-warp` | `ShaderToy/mountain_peak.glsl` | derivative/domain-warped multifractal structure | remove the radial focal mountain and all rendering |

Licensing is part of the boundary, not an attribution afterthought. The local
Rainforest and Canyon sources explicitly restrict reuse to educational
reference. Mountains, Mountain Peak, and Day at the Lake identify CC
BY-NC-SA terms. Swiss Alps depends on texture inputs and a multi-buffer
renderer. The study records these references as evidence while implementing
only independently expressed, general procedural ideas.

## Audit-Only References

`day_at_the_lake_common.glsl` is a cyclic coordinate-deformed 3D implicit
terrain field. Converting it into a heightfield requires a root-solving and
sampling policy that would dominate the comparison, so it is not a v1 source
candidate.

`canyon.glsl` depends on external texture channels for macro terrain and adds
3D displacement during rendering. The local archive does not preserve enough
input metadata to reproduce the terrain source faithfully, and its source
license forbids a direct port. Do not substitute an invented canyon proxy and
label it as that reference.

## Evidence And Promotion

The ignored review pack belongs under
`outputs/terrain/source-model-study-v1/`. It must contain fixed-range top-view
height and slope sheets, common clay views, common presentation views, raw and
calibrated statistics, selected stage coordinates, source throughput, content
hashes, and the capture contract.

A candidate is eligible for a later promotion discussion only when at least two
of the three seeds show coherent broad buildup, ridges with terrain-scale body,
readable summit hierarchy, and no dominant grid, diagonal, contour, focal-mask,
or repeated-template artifact. Throughput is recorded but is not a v1 rejection
gate because the source is baked. Runtime promotion must still preserve the
production stride-three topology and the terrain-only `<1 ms` p95 GPU gate.

This batch stops after the comparison pack. Erosion, hydrology, biomes,
close-range terrain, material detail, and source promotion remain later
decisions.

## Initial Pack Finding

The completed v1 pack does not justify promoting a candidate:

- `control-v2-1` remains the only consistently dramatic silhouette, but its
  high slope density still reads as thin fins, jagged summits, and noise piles;
- `terrain-engine-fbm` and `elevated-derivative` are coherent and stable, but
  their calibrated silhouettes are too subdued and rounded to establish a
  mountain hierarchy;
- `swiss-derivative` is seed-unstable and produces sparse wall-like relief when
  the folded field activates;
- `mountains-signed` supplies broad buildup but not enough ridge or summit
  structure at this world scale;
- `rainforest-cliff` is useful evidence for bounded cliff emphasis, but reads as
  mesas or bulges rather than a mountain range;
- `mountain-peak-warp` is the most promising alternate structure family, yet
  still lacks a convincing range-to-ridge-to-summit hierarchy across the fixed
  seeds.

The study therefore rejects a source-only swap as the next production change.
The useful result is narrower: derivative damping, signed octave coupling,
cliff remapping, and derivative warp are now isolated operators that can inform
a later hierarchical source. Do not resume per-candidate constant tuning in
this lane or mistake the TerrainEngine source for the appearance of its full
tessellation, material, lighting, and scene stack.

Reproduce it with:

```sh
cmake --build --preset dev --target \
  cubey_project_terrain_source_study \
  cubey_project_terrain_source_study_report
projects/terrain/capture_source_model_study.sh
```

Review `terrain-source-study-height.png` and
`terrain-source-study-slope.png` first. The three clay sheets then compare the
same source families across seeds without material camouflage. The final
seed-`9012` presentation sheet tests compatibility with the common renderer;
it is not source truth. `source-report.json` records calibration, field
distributions, local relief, throughput, hashes, and stage plans.

## Mountains-Guided Follow-Up

The later `mountains-hierarchy-v2` recipe corrects the old Mountains study's
scale mismatch by coupling a 7 km envelope, 3 km signed structure chain, and 14
km sparse uplift inside the fixed domain. It is included in the current v2
study registry; the original seven-recipe v1 evidence remains unchanged.

The focused pack under `outputs/terrain/mountains-source-decision-v2/` confirms
that the correction improves seed stability and useful relief relative to
`mountains-signed`, but overcorrects into rounded range silhouettes. It does
not displace v2.1 or qualify for production promotion. The implementation and
verdict are recorded in
[`terrain-mountains-guided-source-correction.md`](terrain-mountains-guided-source-correction.md).
