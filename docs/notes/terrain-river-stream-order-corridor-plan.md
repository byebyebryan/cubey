# Terrain River Stream-Order Corridor Plan

Date: 2026-06-25

This note captures the revision 6 river-quality pivot. Revision 5 proved that
the `stream_order` diagnostic had a more useful river-network source shape than
isolated local tributary picks, but it still used stream order only as a
supplemental path source.

## Decision

Promote `stream_order` and `flow_accumulation` into the river corridor driver.
The active river product should be selected from connected corridors in the
padded hidden routing domain, then traced and rasterized as smoothed channel
paths. The default output should read as one basin-scale river system with a
clear trunk and limited tributaries. The stress recipe may render several
selected corridors so artifacts are easier to see; it is diagnostic, not the
desired default composition.

## Implementation Boundary

- Keep the public terrain product fields and debug view set unchanged.
- Bump the generator revision when the river product changes.
- Do not reintroduce revision 4 direct graph-edge rendering.
- Do not add depression fill, breach routing, erosion, lakes, or new hydrology
  diagnostics in this batch.
- Use the existing padded hidden routing domain, D-Infinity flow direction, and
  fractional accumulation as inputs.

## Selection Model

The extractor should build candidate corridor cells from the existing bucketed
`stream_order` field:

- default trunk support: `stream_order >= 4`;
- default tributary support: `stream_order >= 2`;
- stress trunk support: `stream_order >= 4`;
- stress tributary support: `stream_order >= 2`.

Candidate cells are grouped as connected support in the hidden-domain
`stream_order` field. This replaced terminal-based grouping because unresolved
local sinks fragmented otherwise coherent source shapes. The default recipe
keeps one selected corridor; the stress recipe can render several corridors for
diagnostics. Lower-order tributary branches are accepted only when they connect
back to the selected trunk/corridor, not as independent local features.

## Rendering Model

Selected corridors are still analysis data, not direct product pixels. The
renderer should trace a strongest main trunk and limited connected tributaries,
then use the existing path pipeline:

`resample -> smooth -> constrained offset -> relax downhill -> smooth -> rasterize`

Widths and strengths should vary with stream order or discharge so tributaries
taper into the trunk instead of reading as uniform tubes.

## Acceptance

- `river-mask` is dominated by one connected default component.
- `tributaries` feed the trunk instead of appearing as broken fingers.
- `stream-order` can remain denser than the active product, especially in the
  stress recipe.
- Hard horizontal, vertical, and 45-degree centerline runs should not become
  worse than revision 5.
- The stress recipe should increase coverage without flooding the review patch.
- Remaining limitation: the default review composition can still be sparse or
  close to a crop edge, so the stress recipe remains the better artifact probe.
