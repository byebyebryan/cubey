# Shared terrain readability study V1

Opt-in study; no terrain/solver changes, asset imports, live GUI, default promotion,
push or live terrain streaming. The existing three prepared sites and saved water
are protected by SHA-256. Results live under ignored `outputs/terrain/readability-*`.

## Display atlas

`projects/terrain/study_terrain_atlas_v1.py` consumes the immutable world package.
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
