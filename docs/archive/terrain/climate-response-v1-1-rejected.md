# Terrain Climate Response V1.1 Rejection

Date: 2026-07-21

Status: rejected evidence from an unmerged branch.

## Purpose

This archive preserves the useful result and failure from
`terrain-climate-v1-1` without promoting its experimental climate controls into
Terrain V1. The branch changed only the project-local climate response over five
frozen Terrain Diffusion regions. Elevation, placement, topology, rendering,
palettes, and the production `mineral-control` path remained fixed.

## Result

Every automated identity, numeric, and performance gate passed. The rational
moisture and cover curves recovered useful range across hot/wet, hot/dry, and
cold/dry inputs without hard climate bands. Clear terrain mean/p50 changed from
`0.638851/0.628064 ms` to `0.650266/0.628512 ms`, within the matched budget.

The combined candidate failed the visual gate. Its annual-statistics-derived
cold-season proxy raised mean cool/wet snow from `0.271` to `0.387` and turned
the broad foreground into a snow blanket. Passing numeric cold ranges did not
make that allocation visually or semantically credible.

## Decision

The branch was not merged. `mineral-control` remains the production default,
and climate formulas, region labels, and diagnostics remain terrain-project
experiments rather than foundation semantics.

If this research resumes, treat the findings independently: the rational
moisture and cover response is viable evidence, while snow needs a spatial
allocation model that does not apply seasonal cold as a broad direct
multiplier. The rejected implementation is retained at Git tag
`archive/terrain-climate-v1-1-rejected`.
