# Terrain Ref Closure

Date: 2026-07-10

## Decision

`projects/terrain_ref` is frozen as a buildable visual benchmark. It is no
longer the active terrain implementation lane. Accept maintenance fixes that
preserve its captures and command line, but do not add recipes, generalize its
shader model, or promote its recipe switch into the new terrain product.

The next active lane is `projects/terrain`: a CPU-authored terrain patch product
with explicit fields, regional process diagnostics, and render/export consumers.

## Closure Review

Run the fixed review matrix with:

```sh
projects/terrain_ref/capture_closure_review.sh
```

The ignored output under `outputs/terrain_ref/closure/` contains:

- top-view neutral-height captures for every recipe at seeds `0`, `9012`, and
  `12345`;
- material presentation captures at the same seeds;
- a canonical seed-`9012` near-surface capture for every recipe;
- shape, presentation, and surface contact sheets plus capture metadata.

Water is disabled so source-shape comparisons are not hidden by a flat water
plane. The erosion-filter recipe explicitly shows its post-process surface;
other recipes show their unfiltered source surface.

## Final Status

| Recipe | Closure status | What it establishes | Known limit |
| --- | --- | --- | --- |
| `terrain-engine-ref` | Keep as baseline | Compact coherent world-space height and procedural presentation. | Sparse focal mountains and modest relief. |
| `shadertoy-mountain` | Carry source ideas | Broad mountain support plus warped ridged/billow detail. | Rounded clusters; not a terrain product or process model. |
| `shadertoy-erosion-filter` | Keep as process reference | Selective slope-aware detail can improve a suitable source. | Stateless and non-hydraulic; cannot establish drainage topology. |
| `shadertoy-alpine` | Useful sentinel | Strong seed variation and readable alpine relief. | Detail and material remain coupled to one recipe. |
| `shadertoy-dunes` | Weak sentinel | Wind alignment and scale vocabulary. | Sparse parallel bands still read as waves. |
| `shadertoy-lake-basin` | Weak sentinel | Basin and waterline vocabulary. | Repeats a centered bowl template across seeds. |
| `shadertoy-badlands` | Conditional sentinel | Fine arid incision and strata cues. | Macro shape remains shallow and visually busy. |
| `shadertoy-coast-island` | Weak sentinel | Shoreline and beach-shelf vocabulary. | Repeats a diagonal coast composition. |
| `shadertoy-plains` | Useful sentinel | Stable low-relief terrain and broad swales. | Intentionally provides little structural stress. |
| `shadertoy-gorge` | Weak sentinel | Incision/material contrast vocabulary. | Repeats one central corridor rather than deriving a network. |
| `shadertoy-glacial-highland` | Conditional sentinel | Snowfield, valley, and rock-rib cues. | Rounded and seed-fragile; valley hierarchy is weak. |
| `shadertoy-crater-field` | Useful sentinel | Overlapping multi-scale depressions and rims. | Some spacing and size regularity remains visible. |

## Reboot Handoff

Carry forward:

- deterministic world-coordinate sampling with explicit seed domains;
- the clean-room mountain source as the first source-model donor;
- neutral field views, multi-seed capture matrices, and near-surface review;
- selective process application rather than assuming one filter fits all terrain;
- procedural presentation as a consumer, not as terrain truth.

Do not carry forward:

- the monolithic biome recipe switch or duplicated CPU/GLSL source formulas;
- finite review-mesh coordinates as terrain identity;
- centered bowls, corridors, coastlines, or other patch-composition templates;
- material shading as the only evidence that a heightfield works;
- the stateless erosion filter as a substitute for regional hydrology.

The reference lane succeeded by establishing vocabulary and failure cases. Its
remaining visual defects are input to the reboot, not a queue of fixes for this
project.
