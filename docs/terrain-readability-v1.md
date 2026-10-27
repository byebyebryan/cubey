# Shared terrain readability study V1

Status: dated study and approved promotion from 2026-10-12. The current
[Terrain guide](../projects/terrain/README.md) owns native use; the observations
and qualification below retain their original inputs and acceptance scope.

The original study below was opt-in. After visual approval, the simple shared
material was promoted on 2026-10-12; see the promotion section at the end. Original
report artifacts remain unchanged. The study itself did not change terrain or
solver code, import assets, open a live GUI, push commits or stream live terrain.
The existing three prepared sites and saved water
are protected by SHA-256. Results live under ignored `outputs/terrain/readability-*`.

## Display atlas

`studies/terrain/landscapes/study_terrain_atlas_v1.py` consumes the immutable world package.
It first filters cached native 30 m terrain, then requests only missing native
1024-square tiles with the accepted 64-sample halo and strict producer precision.
The atlas is a 256 m box integral of piecewise-linear native reconstruction,
accumulated in float64 and stored as float32. It is **not** the coarse conditioning
latent and **not** a replacement runtime height source or hydrological survey.

The child is bounded to 900 seconds and retains progress, partial coverage and
logs if interrupted. Coverage code zero means missing, never invented filler.
Only a padded tile and the atlas are retained by the exporter; upstream internal
producer caches remain upstream-owned. No full native world raster is exported.

First run: `outputs/terrain/readability-20261012-a1`, 1025 x 1025, full 262.144 km
coverage; 483156 cached / 567469 generated samples; filtered overlap error 0 m.
Producer phase approximately 270 seconds; see the attempt receipt for total wall
time including cached filtering. Peak process RSS approximately 3.73 GiB.
CUDA peak was not retained in this initial receipt (do not infer a value).

Numerical tests cover normalized constants, affine fields, native phase/tile
invariance and refusal to clamp an unsupported filter halo. They use the existing
optional scientific Python environment, not Torch or a running producer.

## Matched controls

Wide cameras, lighting, source and water remain fixed. Compare retained shading,
clean clay (no material perturbation), periodic detail off, base normal, albedo,
and clean clay with 512 m versus 1024 m maximum exterior geometry spacing.
Paired core/perimeter geometry and hydraulic fields are unchanged by these controls.

The proposed candidate fades colour and roughness as well as normal detail with
pixel footprint, reduces flat-ground contrast and uses world-stable material
coordinates. Stop at the simplest successful candidate. More complex stochastic
tiling is not required unless matched evidence shows it is useful.

Acceptance: legacy-off pixel equality, immutable GPU fields, seam/lifecycle/shared
consumer tests, image and short motion review; two 1080p matched repeats with no
concurrent producer/test GPU work, no readback/encoding in timed windows. Proposed
added GPU p95 budget 0.3 ms; whole scene 8 ms GPU / 4 ms CPU service. These are
local headless bounds, not native GUI or arbitrary-camera/horizon qualification.

## Result and use

Review: <http://starship:8801/terrain/readability-20261012-a1/>. The report starts
with the finer atlas and plains comparison; diagnostic, mountain, water, backdrop,
motion and timing evidence is collapsed underneath. HTTP/index/navigation and
all 36 linked references were checked from Starship, not from remote Snap or a
browser layout test. Three six-second actual orbit clips are available.

The matched clean-clay and periodic-detail-off controls strongly implicate the
material in the conspicuous plains pattern. A 512 m exterior cap changes real
shape/filtering but does not address the repeating material. Candidate A was
sufficient for this pass: colour/roughness as well as normal contrast fade with
footprint, flat substrates have lower contrast, and material XZ follows the
source-world frame instead of restarting at each selected site's local centre.
The existing periodic texture remains at lower amplitude; this is not a claim
of mathematically non-repeating terrain. No stochastic tiling/noise system was
added. Climate/landform weights and the accepted palettes remain unchanged.

The shared helper is `shaders/cubey/terrain/terrain_material_readability.glsl`.
Prepared sites and captured ground use `--terrain-readability refined`; the
established radial backdrop uses `--terrain-surface-detail readable-detail`.
At study time these were UI study selections; the approved refined material is
now the default, and the previous selections remain available for reference.
`--terrain-readability detail-off` removes periodic modulation for diagnosis;
`--terrain-water-ground-view clay-clean|albedo|normal` offers matched views.
`--terrain-context-max-spacing 512|1024` caps exterior spacing explicitly;
zero/default preserves the **original grid-dependent ring schedule**, including
other supported cell sizes. Paired core/perimeter and water geometry are unchanged.
`--terrain-review-orbit-speed 0.04` is a headless render-only camera study control,
not hydraulic acceleration or a changed simulation clock.

All 19 final selected CTests passed without skips. Six final disabled-candidate
comparisons (three sites, three legacy water cameras) are pixel-exact. Immutable
GPU fields, protected source/package hashes, seam checks, shared Fluid regressions,
material GPU endpoints/world-frame controls and headless lifecycle checks passed.

Twelve final 1080p measurements (two repeats x retained/refined x plains,
mountain, existing water) passed the 0.3 ms added-p95 / 8 ms whole-GPU / 4 ms
CPU-service gates. Maximum observed whole GPU p95 was 4.056 ms and CPU service
0.360 ms. Paired candidate p95 did not increase in these repeats; **no speedup is
claimed**. Terrain-only medians were about 0.56–0.59 ms while p95 varied about
0.60–4.06 ms in both modes. The tail variability is retained and not assigned an
unmeasured clock/host cause. Water-scene p95 was about 3.31–3.32 ms. These are
captured-state renderer timings, not live solver contention measurements.

The complete display atlas is published separately under
`cache/terrain/atlases/v1/f12244a1ff943e37/256m/`, with grid, source transform,
producer pin, payload hashes and coverage codes. It does not modify the immutable
world packages or become a runtime height source. `--phase publish` refuses an
existing destination and invalid/partial/nonfinite/hash-mismatched data.

Reproduce using a fresh `outputs/terrain/readability-*` leaf (existing optional
producer/scientific environment and pinned world cache required):

```sh
python3 studies/terrain/landscapes/study_terrain_atlas_v1.py OUTPUT --phase freeze
python3 studies/terrain/landscapes/study_terrain_atlas_v1.py OUTPUT --phase generate
python3 studies/terrain/landscapes/review_terrain_readability_v1.py OUTPUT --phase controls
python3 studies/terrain/landscapes/review_terrain_readability_v1.py OUTPUT --phase refined
python3 studies/terrain/landscapes/review_terrain_readability_v1.py OUTPUT --phase motion
# Stop producer/GPU tests/encoding before this phase; freeze binary/shaders.
python3 studies/terrain/landscapes/review_terrain_readability_v1.py OUTPUT --phase performance
# Use the installed terrain-diffusion scientific Python for maps and publication.
cache/terrain/tooling/v1/terrain-diffusion-venv/bin/python studies/terrain/landscapes/report_terrain_readability_v1.py OUTPUT --phase maps
# Final selected CTest receipt must be OUTPUT/final-tests.xml.
python3 studies/terrain/landscapes/report_terrain_readability_v1.py OUTPUT --phase report
python3 studies/terrain/landscapes/report_terrain_readability_v1.py OUTPUT --phase http
```

The original study deferred promotion until visual review (now approved).
Remaining limits are native
30 m geometry, no vegetation/clutter, fixed filtered exterior geometry, unqualified
far shadows/horizon and finite recorded-water boundaries. No push was performed.

## Approved default promotion

The default is now `refined` for prepared-site/captured ground and
`ReadableDetail` for the radial backdrop and its shared runtime frame contract.
The shared Macro recipe defaults to readability mode 2, so the Fluid mountain
demo stays aligned. V1/refined/terrain Fluid reference recipes retain mode 0.
Both recorded native crops and sampled grids carry their already-validated
source-world centre into material uniforms; synthetic fixtures retain origin zero.
This adds no terrain sampling, solver work, texture generation or GPU dispatches.

Previous ground remains selectable using Terrain's `--terrain-readability current`
or backdrop `--terrain-surface-detail filtered-detail`. Fluid exposes **Terrain
detail** in its material UI, and its existing v2 tuning JSON accepts
`"terrain_readability": 0` (previous), `1` (periodic detail off), or `2` (readable).
Only ground shading changes; water optics/foam/rain, bank treatment and the
original grid-dependent mesh density stay unchanged.

`check_readability_defaults_v1.py` performs genuine headless checks in a fresh
`outputs/terrain/readability-defaults-*` leaf. All three site defaults, wider-water
default and radial backdrop default match the approved opt-in PNGs pixel-exactly.
The previous Terrain and Fluid reference material still reproduces its prior
PNG exactly. Terrain/Fluid default water views differ by at most one 8-bit value,
the retained UNORM/sRGB attachment rounding boundary. Immutable GPU recording
and source/world-centre receipts are required. The approved study is never rewritten.

Promotion review: <http://starship:8801/terrain/readability-defaults-20261012-a1/>.
No GUI, new simulation or terrain generation is needed for promotion.

Historical V1 capture comparisons explicitly select the previous material;
default-promotion checks omit that override. This preserves reference meaning
without relying on whatever the current app defaults happen to be.
