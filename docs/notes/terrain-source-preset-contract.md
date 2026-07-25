# Terrain Source Preset Contract

Date: 2026-07-25

Status: accepted for implementation.

## Decision

Terrain Diffusion is a source of curated natural locations, not an explicitly
controlled biome or landform generator. Runtime names describe the broad
visual result, while exact seed, origin, producer revisions, and payload hashes
remain the reproducible identity.

The committed catalog is:

```text
projects/terrain/terrain_source_presets.json
```

It contains one required default and a small optional catalog:

- `mountain-backdrop-1` is the canonical default;
- `alpine-range-1`;
- `mountain-valley-1`;
- `rolling-hills-1`;
- `rolling-lowland-1`.

Numbered semantic families allow another visually distinct source to be added
without claiming that Terrain Diffusion can request a precise geological
feature. Study names such as `canyon-candidate-2` remain provenance only.

## Generation Boundary

The default contract is one preset and one Terrain Diffusion region query:

```text
cubey_terrain_generate_default_asset
```

Normal configure, build, test, and application startup never generate terrain.
Optional presets are generated individually through explicit targets. A
curated optional preset directly queries its pinned seed and coarse origin; it
does not rerun climate scans, morphology probes, or ranking.

The existing landscape-variation and desert/canyon generators remain
developer-only study targets. They are not dependencies of the default or
optional preset paths.

## Storage Boundary

The repository stores only:

- the versioned recipe catalog;
- generator and validation code;
- expected elevation and climate hashes;
- documentation and tests.

Generated heightfields, climate fields, previews, and renderer evidence stay
under Git-ignored `cache/` and `outputs/` roots. Each optional preset has an
independent cache directory, so it can be generated or removed without
affecting the default or another preset.

Future distribution may publish generated bundles through an artifact or
download cache. That affects installed data size, not Git history.

## Runtime Boundary

The terrain application may enumerate every committed optional recipe, but
must not treat a missing bundle as an error. Each recipe reports one of:

- `available`: both bound manifests are complete;
- `not generated`: no bundle exists;
- `generating`: an explicit external generation target owns the marker;
- `incomplete`: only part of the expected bundle exists.

Only available presets are selectable. The renderer does not launch the
Terrain Diffusion Python environment or a long-running CUDA generation job.
