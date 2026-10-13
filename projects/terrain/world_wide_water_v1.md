# Prepared-world wider water V1

Status: in progress. Shared Terrain captures first; the live Fluid demo is unchanged.

Extend the established wider-water bake/export and fixed paired terrain/water
topology to the three pinned prepared TerrainSite packages. Preserve elevations,
solver math/protocol, legacy assets, source-aligned materials and numerical defaults.
No generation, live-core/background splice, rain-off classification or default promotion.

Candidate grids: 1024²/128 m and 2048²/64 m both cover a nominal 131.072 km square;
their actual sampled spans are 130.944 and 131.008 km. A 2048²/32 m fallback covers
65.504 km sampled. Each is a coherent independent hydraulic state, not a coupled
LOD solver. Different sites are not one common global water solution.

Reuse the native LOD0 sampler, unchanged external worker and solver-neutral
capture exporter. Same dry start, 512 mm/h demonstration rain, Manning 0.05 and
open perimeter. Retain 2/6/12-hour snapshots; no claim of ordinary weather,
equilibrium, conservation proof or permanent-water classification. Per-run
execution is bounded at 1800 seconds plus startup/owned-shutdown allowance.

Mesh preflight retains the established maximum 64 m exterior draw spacing.
An explicit vertex budget may rise from the unchanged 2M default to at most 5M;
the study uses 4.5M. Finer mesh interpolation does not restore missing simulated
channels. Keep the old aggressive-coarsening rejection rather than disguising
the cost with degraded distant wet silhouettes.

Sequence: pin packages and preflight geometry; three coarse pilots; original
mountain full-coverage confirmation; visual/resolution review; conditional further
confirmations or finer fallback; opt-in site/capture binding; headless lifecycle,
legacy parity and repeated render-only costs. Proposed 1080p scene gates retain
GPU p95 <=8 ms and CPU service <=4 ms. Report memory/setup costs and every failed
or partial attempt. Stop solver workers before rendering performance trials.

Water references live in separate bindings: do not mutate pinned site manifests.
The site owns its source; the optional capture must match site/world/source hashes,
centre and admitted coverage. The Terrain consumer runs no solver and must label
capture time/grid and decorative surface motion. All coverage is finite; bounded
orbit views and deliberately exposed outer boundaries must be reviewed separately.

Evidence root: `outputs/terrain/wider-water-world-20261012-gqeluh/`.
Validated checkpoints are committed; no push or default promotion.
