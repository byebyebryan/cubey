# Terrain Project Map

Date: 2026-07-22

This map records the terrain product-promotion boundary. Older river,
mountain-driver, source-model, and landscape-evolution notes are historical
evidence rather than an active queue.

## Product And Studies

| Location | Role | Change policy |
| --- | --- | --- |
| `projects/terrain` | Fixed-focus external-heightfield backdrop, shared runtime, atmosphere, material review, diagnostics, and capture. | Active product and review host. One runtime path and one asset contract. |
| `studies/terrain/reference` | TerrainEngine and curated clean-room ShaderToy visual controls. | Optional frozen study. Maintenance and reproducibility fixes only. |
| `studies/terrain/shadertoy` | External-source fidelity comparison between original raymarches and Cubey mesh transfer. | Optional restricted-source study. Do not promote source code directly. |
| `studies/terrain/hydrology` | Regional drainage, graph routing, and analytical landscape-evolution experiments. | Optional paused study. Not a terrain-product dependency. |

The failed workbench, terrain-lab, and coastal procedural-terrain
implementations are removed after their conclusions are archived. Git history
is the implementation record. `projects/planet` remains a distinct
planet-scale renderer whose local terrain field does not define this product.

## Product Spine

```text
offline generator
    -> cubey.terrain.heightfield.v1 asset
    -> worktree source validation/cache
    -> deterministic natural placement
    -> persistent derived continuous sector mesh
    -> filtered procedural material + shared atmosphere
    -> shared GPU runtime
    -> terrain review UI + opt-in glTF Viewer consumer
```

The generator is replaceable and does not run in the renderer. Regional
simulation products can be added later through explicit adapters; they do not
define this asset or rendering contract.

## Source And Asset

The canonical development field is generated locally from the pinned Terrain
Diffusion 30 m model at seed 0. It is one accepted product asset, not the terrain
API. Any producer may supply the same manifest and regular elevation contract.
Terrain shape stays unchanged during rendering and material work. Production
fields remain outside Git; one small deterministic tracked raster exists only
to exercise both real consumers in normal headless tests.

## Process Boundary

The product has no terrain process pass. D8, D-infinity, graph rivers, priority
filling, stream-power evolution, and particle hydrology belong in the optional
hydrology study or a future hydrology reboot.

## Consumer Boundary

The terrain app and glTF Viewer are the current consumers. The shared asset,
product, and runtime APIs have moved into `include/cubey` and `src/cubey`; glTF
keeps terrain explicitly opt-in and preserves its no-terrain path. Fluid,
ocean, planet, and other adapters remain deferred until a concrete composition
needs the backdrop contract.

## Review Contract

Review always includes:

- canonical source height and slope views;
- unrestricted headings and camera-envelope endpoints;
- flat and filtered-detail material comparisons;
- clay, normal, material, edge, and stage diagnostics;
- neutral and raking shared-atmosphere lighting;
- topology, performance, provenance, and capture metadata.

Material shading cannot be the only evidence. The height and slope views must
show the same macro hierarchy, and outputs must be grouped by checkpoint rather
than accumulated as an undifferentiated image dump.

The external-source fidelity lane remains a narrower exception to the
clean-room product boundary. Restricted source remains outside Cubey and its
findings must be re-expressed independently before they influence product code.

## Deferred Work

- material and terrain-light-response refinement beyond the current candidate;
- hydrology, rivers, lakes, wetlands, and coastlines;
- biome/climate products and foliage placement;
- terrain deformation, persistence, colliders, and streaming;
- additional cross-project adapters beyond glTF Viewer;
- spherical planet mapping, floating origin, and planet-scale streaming;
- close or hero terrain rendering.
