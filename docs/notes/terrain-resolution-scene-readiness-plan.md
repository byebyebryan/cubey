# Terrain Resolution Scene-Readiness Plan

Date: 2026-07-04

Revision 30 improved the mountain stress recipe, but the result still raises a
separate question: can the current terrain workbench scale from local review
patches to a scene view with enough detail?

The next batch should answer that with a fixed-extent audit before changing the
renderer architecture. The current CLI already exposes both `--grid-size` and
`--cell-size`, so higher resolution can be tested without introducing new
interfaces.

## Target

Compare the same approximate world extent at multiple sample densities:

| Label | Grid | Cell size | Approx extent | Mesh cost |
| --- | ---: | ---: | ---: | ---: |
| fast review | `513` | `32m` | `16.384km` | `~0.52M` tris |
| detail review | `1025` | `16m` | `16.384km` | `~2.10M` tris |
| stress detail | `2049` | `8m` | `16.384km` | `~8.39M` tris |

This separates "larger patch" from "more samples per meter". Raising grid size
without reducing cell size mostly increases patch extent; it does not answer
whether the terrain has enough local detail for a scene view.

## Review Outputs

Write ignored local captures under:

```text
outputs/terrain/resolution-mountain-16km/513
outputs/terrain/resolution-mountain-16km/1025
outputs/terrain/resolution-mountain-16km/2049
```

For each resolution, inspect at least:

1. `mountain-process-review.png`
2. `mountain-perspective.png`
3. `mountain-post-erosion-perspective.png`
4. `mountain-profile-height.png`
5. `slope-instability.png`

Record generator revision, field count, review output count, field statistics,
capture timing, and max resident memory. The key judgment is whether artifacts
improve with resolution or remain source/process-model failures.

## Expected Outcome

Do not promote `2049` to the default. Keep:

- `513` for fast iteration;
- `1025` for normal review;
- `2049` for stress/detail checks.

If `2049` only reveals more of the same source-shaped artifacts, the next
terrain-quality work should focus on source/process models rather than raw
resolution. If resolution materially improves the scene read but is too
expensive as one mesh, the next architecture batch should plan a local terrain
clipmap or tiled patch renderer.

## Audit Result

The fixed-extent audit completed for all three sample densities under:

```text
outputs/terrain/resolution-mountain-16km/513
outputs/terrain/resolution-mountain-16km/1025
outputs/terrain/resolution-mountain-16km/2049
```

Each directory contains the scalar review set, `manifest.json`,
`mountain-perspective.png`, and `mountain-post-erosion-perspective.png`.

| Grid | Cell size | Scalar export | Preview height | Preview post-erosion | Scalar max RSS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `513` | `32m` | `53.64s` | `50.37s` | `48.50s` | `154640 KB` |
| `1025` | `16m` | `123.53s` | `107.39s` | `100.61s` | `479060 KB` |
| `2049` | `8m` | `513.81s` | `453.56s` | `422.15s` | `1684872 KB` |

The `2049` preview path peaked around `1830172 KB` RSS, so it should remain a
stress path rather than a default review path.

The manifests all report generator revision 30, 55 fields, and 50 scalar/review
outputs. Fixed-extent field summaries:

| Grid | `height_m.span` | `mountain_profile_height_m.span` | `mountain_mass.mean` | `mountain_shoulder.mean` | `mountain_summit_core.mean` | `slope_instability.mean` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `513` | `1695.575` | `1562.146` | `0.4465` | `0.3861` | `0.0356` | `0.0643` |
| `1025` | `1611.336` | `1617.425` | `0.4673` | `0.4134` | `0.0441` | `0.0979` |
| `2049` | `1610.229` | `1574.452` | `0.4331` | `0.3754` | `0.0124` | `0.1354` |

Visual conclusion: higher sample density improves silhouette smoothness and
local readability, but it does not fix the mountain source model. At `1025` and
especially `2049`, the same source/process artifacts become easier to diagnose:
crisp ridge connector arcs, visible banding in shoulder/process fields, and
some terrace-like buildup that comes from the current driver rather than from
undersampling. The next terrain-quality batch should therefore focus on source
and process model changes before spending more effort on raw resolution.

The scene-readiness conclusion is separate: if this workbench needs large
scene-scale detail, a single full-resolution mesh is the wrong default. Keep
`513` for fast iteration, use `1025` for normal fixed-extent review when needed,
and reserve `2049` for occasional artifact hunts. A future renderer path should
plan tiled terrain or clipmap-style local detail instead of promoting the
stress mesh as the main capture architecture.

## Workbench Compromise

The current cost is acceptable only as a diagnostic tradeoff. The terrain
workbench keeps many named CPU fields because that makes source drivers, process
operators, product fields, and regressions inspectable. It also writes many PNGs
because field-by-field review is more useful at this stage than a single pretty
render.

That is not the target runtime model. A scene or planet terrain path should not
retain the full debug product at every visible sample, export every field, or
draw one giant mesh. It should use tiles, clipmaps, view-dependent shader detail,
and a much smaller runtime field set. Multithreading can make the workbench less
painful, but it should follow phase timing evidence and should not be treated as
the answer to LOD or to-horizon scale.

## Surface View And Phase Profiling

The follow-up pass added terrain-local phase profiling and two lower camera
presets, `surface` and `surface-low`, so the same fixed-extent patch can be
reviewed closer to the ground. Both `terrain` and `terrain_preview` accept
`--profile-output <prefix>` and emit `<prefix>.terrain_phases.json`; bare
prefixes resolve under `outputs/profiles/`.

The surface captures used the same `16.384km` mountain stress patch:

| Grid | Camera | Color | Generate | Mesh build | Host render | Total |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| `513` | `surface` | `material` | `47.74s` | `1.94s` | `0.35s` | `50.06s` |
| `513` | `surface` | `height` | `47.56s` | `0.14s` | `0.34s` | `48.06s` |
| `513` | `surface-low` | `height` | `47.61s` | `0.14s` | `0.33s` | `48.11s` |
| `1025` | `surface` | `material` | `96.89s` | `7.76s` | `0.37s` | `105.03s` |
| `1025` | `surface` | `height` | `96.27s` | `0.56s` | `0.36s` | `97.21s` |
| `1025` | `surface-low` | `height` | `96.84s` | `0.54s` | `0.35s` | `97.75s` |
| `2049` | `surface` | `height` | `412.75s` | `2.24s` | `0.42s` | `415.44s` |

The result is clear enough for planning: CPU generation dominates by a large
margin, not host rendering. Material preview mesh construction is the main
secondary cost because it samples more product fields per vertex than
height-color preview. CPU multithreading should therefore start by measuring and
parallelizing generation phases if this workbench remains useful, but it will
not change the scene-readiness conclusion by itself.

The visual conclusion also stayed the same. `1025` and `2049` surface views
make the terrain easier to inspect from a player-like angle, but they mostly
clean the mesh silhouette and expose the current source/process limitations:
smooth foreground slopes, synthetic shoulder/ridge structure, and little
near-field detail. A future scene path needs tiled or clipmap terrain with
view-dependent detail, while terrain quality work still needs better coherent
source/process models.
