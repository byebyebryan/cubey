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
