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

## Integration and review controls

`--terrain-site SITE/site.json --terrain-site-water ASSET/water-binding.json`
loads a coherent saved state through the shared Terrain capture consumer. The
separate binding pins site, world recipe, heightfield and capture metadata; capture
payload/source/centre/coverage validation remains mandatory. `--terrain-site`
alone stays dry, and the old capture/source override restrictions stay in force.
Use `--terrain-water-vertex-budget 4500000` explicitly for the 131 km topology;
the default remains 2000000. Field uploads and topology/context builds are once
per loaded state. Surface animation is not hydraulic advancement.

`study_world_wide_water_v1.py init|geometry|prepare|run|export|publish STUDY`
reuses the legacy bake helpers. Preparation and field audits need the existing
optional native IO Python; the launcher/review controls use ordinary Python.
`review_world_wide_water_v1.py freeze|capture|lifecycle|negative|orbit|performance|report STUDY`
pins the renderer, locks the retained local camera, and keeps all performance
controls separate from hydraulic traffic. No missing cache triggers generation.

The initial geometry preflight passed coverage/winding for all candidates. The
131 km / 64 m topology has 4,409,905 vertices and 8,803,432 triangles, using
140,920,424 mesh bytes. The 128 m pilot is visibly too smooth near the site, so
it is retained as a coarse control, not a replacement for the detailed core.
The 64 m confirmations improve stream/terrain structure and continue water past
the old inner rectangle. Their twelve-hour field states include very deep basins
and broad sheet flooding under exaggerated rainfall; these are not labelled
normal lakes or a common global equilibrium.

Headless validation has passed 31 focused CPU/GPU test groups, four additional
preflight/helper groups, nine legacy/default capture checks (all referenced images
exact; cross-frontend rounding <=1/255), mismatched-site/world admission failures,
and per-site backdrop/water/context transitions with exact immutable GPU fields.
The existing Fluid executable and all shaders/exporter are byte-identical to the
previous qualification; only Terrain's executable changes. Native GUI acceptance
and water-default promotion remain deferred.

## Final qualification

All seven bakes reached twelve physical hours within the 30-minute execution
bound: three 128 m pilots, three 64 m confirmations, and the original mountain's
32 m / 65 km alternative. Observed total wall times were about 2.48 minutes for
each pilot; 12.66 / 13.74 / 9.33 minutes for the full mountain/interior/valley
confirmations; and 18.18 minutes for the finer alternative. These include startup,
teardown and concurrent preparation/correctness/capture work and are not isolated
maximum-throughput benchmarks. The worker's requested cap remains 300x.

Seven field/hash/native-completion/rain-supply/export audits passed with no
nonfinite fields, negative depth, dry momentum or leftover owned groups. Three
milestones per case retain 2/6/12-hour states. These are sanity and byte-parity
checks, not a complete boundary-flux conservation ledger or calibrated hydrology.

The twelve full-coverage water-on timing rows and their ground controls passed
at 1080p/60 Hz: GPU scene p95 3.881–4.018 ms, CPU service p95 <=0.334 ms.
Device-local heap usage peaked at 759.06 MiB; per-run peak process RSS was
703.84 MiB. Water/ground p95 differences were 1.847–1.970 ms, not isolated pass
attribution. The four finer-alternative rows also passed: GPU 3.029–3.364 ms and
CPU <=0.349 ms. All 32 matched runs exclude warmup, image readback and encoding;
the solver was stopped. No wider-live combined-GPU or native-window FPS claim.

Verdict: useful opt-in landscape-water assets and a reusable shared-site
attachment contract. The 32 m / 65 km alternative is sharper in the locked local
view; 64 m / 131 km provides wider coherent coverage at a modest rendering cost.
Keep both independent options. Neither is a nested/coupled solver or a depth
splice, and changing extent changes the contributing catchment as well as detail.

The inner 16 km wet rectangle is removed, but the outer world remains finite.
Some sun-facing orbit headings expose bright reflective sheets over widespread
thin floodwater; the complete orbit/free-flight envelope is not visually accepted.
Full-size states retain maximum depths of 180.56–533.51 m in localized generated
basins under the exaggerated storm. No permanent-lake/equilibrium claim or hidden
per-site forcing adjustment is made. Native GUI review and default promotion are
still deferred; the live Fluid launcher/solver and its binaries/shaders remain
unchanged.

Report: <http://starship:8801/terrain/wider-water-world-20261012-gqeluh/>.
Content-identified optional bindings/captures are published under
`cache/terrain/water/v1/world-wide/`; each site's `published-*.json` receipt gives
its exact matching `--terrain-site` and `--terrain-site-water` paths.
All four published-cache reloads reproduce the reviewed local captures exactly.
The report's 38 linked resources returned HTTP 200; all ten held-state clips
validate as 1280x720, 120-frame media. This is HTTP/media evidence, not browser
layout, native GUI or human visual acceptance.
