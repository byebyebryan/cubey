# Cached Terrain Water site expansion V1

This pass expands **existing unmodified Terrain Diffusion terrain**, not
surveyed elevation data or authored rivers. The original Mountain valley live
1k/16m default, rainfall, solver, appearance and session protocol stay unchanged.

## Selection method

The cached corpus contains 11 distinct elevation payloads with climate data.
Generation-preset aliases are deduplicated by elevation hash. A deterministic
3×3 crop-center grid screens 99 existing 16.384 km-square areas. Every center
also accommodates the 32.768 km advanced area without shifting.

Shared `projects/terrain/tools/terrain_grid_sampling.py` validates and samples
the original heightfield. Existing `pyflwdir` D8 priority-flood and upstream-area
tools provide **disposable analytical locators** at 256²/64m. Filled elevations
never enter the simulation or renderer. No new routing/solver mathematics or
Terrain Diffusion generation was added.

Two candidates each target collections, joining drainage and steep runoff.
All six hydraulic pilots use the same 512²/32m grid, dry start, continuous
512 mm/h uniform rain, Manning 0.05 and fall/open boundary. The target is six
physical hours. The three provisional choices repeat at the default 1024²/16m
grid with exactly the same forcing and footprint. This is an exaggerated
visual-demo storm, not calibrated hydrology.

## Curated behaviours

- **Upland basins**: several visibly growing collections in featureful upland
  terrain. Source `landscape-variations/dry-upland`, center `[12000,-12000]` m.
- **Joining valleys**: tributaries and elevation change feeding a broader main
  collection. Source `desert-canyon-study/canyon-candidate-3`, center
  `[12000,0]` m. The cached directory name does not prove a canyon classification.
- **Steep ridges**: pronounced relief and fast drainage with relatively little
  broad slow pooling. Source `desert-canyon-study/canyon-candidate-4`, center
  `[-12000,0]` m.

The highest routing-score candidate was not selected: its actual rain response
was broad shallow wetting over smooth terrain, not a readable stream network.
The second basin and shifted Alpine candidates remain viable reserves, not
numerical failures. No verified overflow or calm-lake claim is made.

At six physical hours in the 1k confirmations:

| Site | Stored water, million m³ | Largest slow ≥1m patch, km² | Wet speed P95, m/s |
| --- | ---: | ---: | ---: |
| Upland basins | 732.20 | 19.08 | 3.58 |
| Joining valleys | 436.77 | 5.48 | 6.54 |
| Steep ridges | 100.58 | 0.27 | 11.92 |

“Slow” means speed below 0.5m/s; the speed percentile considers cells at least
0.1m deep. These thresholds describe observed fields, not hydrological classes.

## Catalogue and foundations

[`terrain_water_sites.json`](terrain_water_sites.json) owns Fluid's **crop
recipes**, not a second terrain generator or source format. Each recipe pins an
existing Terrain source manifest, elevation SHA and physical crop center. The
source elevation/climate descriptors and world-coordinate sampling remain the
shared Terrain foundation. Preparation and launcher admission check source
identity; admission also checks crop coordinates and named grid geometry.

The bounded catalogue supports six sites × five existing presets = 30 cases,
within the existing 32-entry/64 KiB launch-control limits. Missing or modified
cases remain visible but disabled. The GUI iterates the admitted catalogue;
adding a site no longer requires a C++ name allowlist or UI edit.

Changing a site/grid still means **selection + Apply & restart**. It creates a
fresh immutable session, dry and paused, preserving acknowledged rain and pace.
There is no transfer/regridding of existing water. The native backend schema,
one-controller rule and owned-worker lifecycle are unchanged.

## Evidence and how to read it

The report is `outputs/fluid/site-expansion-20261011-jtIA3s/index.html`, served
through the existing report index at
`http://starship:8801/fluid/site-expansion-20261011-jtIA3s/`.

Overview and flow-inspection images use real Vulkan offscreen rendering of
native fields. No GUI, X11, Xvfb or software framebuffer is involved. Look for
tributaries joining, collections expanding, and preferential drainage along
slopes; rain supplies the entire map and the perimeter allows water to leave.

Formation clips compress six physical hours into ten seconds. Separate late
flow-cue clips show two physical minutes near the end at 12×. Dots/trails and foam
illustrate native velocity but are not native particles or conserved dye.
Sealed binary captures retain actual irregular solver-boundary timestamps and
unchanged h/q payloads, with capture hashes and strict time order. Only sealed
binary recordings gained irregular-time support; native ASC recordings and
growing file streams retain their exact-cadence contract.

The field audit checks retained hashes, finite/nonnegative fields, zero dry
momentum and retained volume not exceeding rainfall supply within a 2% sanity
tolerance. It is **not** a native flux-conservation or physical-accuracy proof.
The largest slow ≥1m connected patch is an observation metric, not a lake label.
Source inputs remain immutable; cleanup receipts cover only owned processes.

Final validation admitted all 30 prepared cases, including 15 byte-identical
original cases. The 300-second offscreen script acknowledged all 46 actions:
every new site/grid combination was installed dry, briefly resumed and paused,
then the original Mountain 1k default was restored. Sixteen replacements and
17 bit-exact bed/h/velocity GPU audits passed, with zero viewer hydraulic
dispatches and clean owned-worker shutdown. Contract/launcher tests, optional
survey diagnostics and shared Terrain sampling tests passed. The receipt is
`review-receipt.json`; detailed lifecycle data is
`switch-validation/validation.json`.

Pilot/confirmation wall times include startup/teardown and concurrent work;
they are not isolated solver benchmarks. Source spacing remains 30m; the 8m
simulation preset interpolates it rather than inventing finer terrain. A 60×
pace request is not guaranteed for every terrain/resolution. Human GUI review
and new-site steady-state presented-FPS measurements remain separate checks.

## Reproduction without installs or generation

Use the already installed optional environments. Screening needs NumPy/SciPy/
pyflwdir; preparation and runs need the existing SynxFlow IO environment. The
audited native extension is hash-checked before a hydraulic run.

```sh
rtk proxy <hydrology-python> survey_terrain_sites_v1.py screen --out <fresh-owned-root>
rtk proxy <native-python> survey_terrain_sites_v1.py run --out <root>
rtk proxy <native-python> survey_terrain_sites_v1.py capture --out <root>
# Review six pilots; write a bounded selected crop catalogue before confirmation.
rtk proxy <native-python> survey_terrain_sites_v1.py run --out <root> \
  --catalogue <selected-crops.json> --configuration patch-1024-16m --wall 600
rtk proxy <native-python> survey_terrain_sites_v1.py prepare-presets --out <root> \
  --catalogue <selected-crops.json> --baseline-study <original-prepared-study>
rtk proxy <native-python> survey_terrain_sites_v1.py motion --out <root> \
  --catalogue <selected-crops.json>
rtk proxy <native-python> survey_terrain_sites_v1.py audit --out <root>
rtk proxy <native-python> survey_terrain_sites_v1.py report --out <root>
```

Run from this directory with absolute paths, or prefix the script path from the
repository root. The output root must be a fresh owned `site-expansion-*` leaf
directly under `outputs/fluid`. Screening is capped at 12 unique cached sources;
six hydraulic pilots are frozen before execution. Every trial has a wall cap.
All-preset preparation is gated on completed default-grid confirmations to
prevent two preparation writers sharing a case. Sealed visual receipts are
not overwritten on rerun. No automated score alone promotes a site.
