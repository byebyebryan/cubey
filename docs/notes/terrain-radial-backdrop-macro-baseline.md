# Terrain Radial Backdrop Macro Baseline

Date: 2026-07-16

Status: accepted macro composition and promoted radial-v1 backdrop source;
detail, setup persistence, and `<1 ms` runtime performance remain open.

## Decision

The `expanded-radial` v2 study established the terrain backdrop's macro target:
a useful foreground, gradual restoration of distant mountains, unrestricted
yaw, and no exposed hard cutoff or one-sided uplift shelf. The accepted
configuration is now frozen by `radial-v1`:

- `32.768 km` outer radius;
- source-derived 6 km low-relief footprint with 8 percent retained relief;
- broad structure restored over `1-24 km`;
- source detail restored over `5-30 km`;
- 500 m focus height;
- 100-1000 m orbit, defaulting to 400 m;
- unrestricted yaw and 0-30 degree elevation.

The original review evidence remains under
`outputs/terrain/radial-backdrop-expanded-v2/`. The production evidence is
under `outputs/terrain/radial-backdrop-product-v1/`.

## Accepted Scope

The radial envelope, expanded domain, and far-field camera composition are
accepted. Across tested seeds and yaw headings, scene views hide the circular
transition, preserve a quiet foreground, and retain useful terrain. The exact
circular gates remain visible in top diagnostics. That is an explicit
fixed-focus composition compromise, not a general terrain model.

The graduated source is frozen as a scalable far-field source. It replaces the
study-only duplicate and preserves exact source reports and baked study output.
It is not accepted as a close-view mountain model.

## Productization

The cached radial study proved stride 3 preserved the macro image. The promoted
product now:

1. wraps the graduated source with radial relief during setup;
2. bakes positions, normals, material classification, and ambient visibility;
3. renders reduced stride-3 indices in culled polar sectors;
4. supports continuous and consumer-owned centers;
5. preserves the accepted camera envelope and unrestricted yaw.

Exact product/study PNG parity is enforced by
`projects/terrain/capture_radial_backdrop_product.sh`. Historical hard-cut
behavior remains available only through explicit `hard-cut-v1`.

## Remaining Gaps

The promotion does not accept:

- current smooth faces and sparse secondary silhouette detail as final;
- the current procedural material bandwidth as sufficient for mid-field views;
- setup-time cost and memory as a scalable streaming design;
- the measured GPU pass as meeting the engine's `<1 ms` backdrop target;
- a surface camera, foliage, water, hydrology, translation, or planet scale;
- radial attenuation as an engine-level procedural-terrain primitive.

The product pack measured `2.552 ms` terrain-surface p95 at 2560 x 1440 under
its maintained active-clock profile, while an isolated identical profile
measured `1.35 ms`. This power-state sensitivity is documented in
[`terrain-cached-radial-integration.md`](terrain-cached-radial-integration.md).
Performance remains debt, not a completed acceptance gate.

## Detail Follow-up

Keep macro composition frozen while improving fidelity in two separate lanes:

- Source/geometry detail: add footprint-filtered secondary ridges, face breakup,
  and silhouette bandwidth without converting mountains into high-frequency
  noise.
- Rendering detail: improve procedural albedo, roughness, normals, and
  restrained atmospheric contrast with distance-aware filtering.

Use close debug views to expose defects, but accept v1 against the far-field
backdrop distance. A later mid-field or traversable product needs its own
source, LOD, material, and content contract.

## Stop Condition

Do not retune the radial center or transition unless a maintained product pack
exposes a ring, foreground intersection, empty yaw sector, or framing failure.
Do not mix detail work, performance optimization, hydrology, or streaming into
the same corrective batch.
