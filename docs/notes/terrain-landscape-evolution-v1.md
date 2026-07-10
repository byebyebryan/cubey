# Terrain Landscape Evolution V1

Date: 2026-07-10

## Decision

The next terrain candidate is `upland-landscape-evolution-v1`. It treats the
existing broad OpenSimplex terrain as an initial condition and its
`uplift_potential` as the driver for a regional landscape-evolution solve. The
contour and broad-noise recipes remain unchanged controls; neither is promoted
as the intended mountain model.

The candidate follows the transient analytical stream-power model described by
Tzathas, Gailleton, Steer, and Cordonnier in *Physically-based analytical
erosion for fast terrain generation*. The production implementation is
clean-room code derived from the published equations. The authors' reference
implementation under `~/code/ref/analytical-terrains` is licensed only for
research and evaluation and is used solely as an external oracle. No source
from that repository may be copied, linked, vendored, or required by Cubey.

The MIT-licensed `~/code/ref/MultiScaleErosion` project remains the preferred
reference for a later detail-amplification stage. It is intentionally outside
this batch so macro formation and fine detail do not obscure each other.

## Model Contract

The canonical review region is `513x513` samples at `100 m`, or `51.2 km` per
side. The solve adds a hidden `64`-sample guard on every side and publishes
only the requested interior. The guard owns the finite domain boundary; the
visible interior is not forced toward a square outlet frame.

Version 1 uses fixed recipe parameters:

| Parameter | Value |
| --- | ---: |
| Terrain age | `1.6e6 years` |
| Stream-power exponent `m` | `0.4` |
| Stream-power slope exponent `n` | `1.0` |
| Stream-power coefficient `k` | `2e-5` |
| Maximum uplift | `1e-3 m/year` |
| Hillslope coefficient | `0.1` |
| Hack constant | `1.5` |
| Hack exponent | `0.6` |
| Thermal coefficient | `1e-3` |
| Critical slope | `tan(30 degrees)` |
| Multigrid levels | `4` |
| Network/elevation iterations per level | `6` |
| Fixed-point update weight | `0.25` |
| Upsampling jitter | `+-0.25 cell` |
| Altitude-correction iterations | `50` |
| Altitude-correction learning rate | `0.01` |

The source height remains world-space procedural truth. Uplift rate is
`uplift_potential * 1e-3 m/year`; no authored ridge, river, peak, or basin mask
is introduced. The receiver random variable is deterministic from world seed,
multigrid level, and aligned sample coordinate.

## Process

Each multigrid level performs the following sequence:

1. Resolve depressions into breach receivers that reach the outer guard.
2. Select one of the four lower direct neighbors with probability proportional
   to elevation drop, using a fixed random value per sample.
3. Build acyclic river trees, accumulate drainage area downstream, and compute
   the full-gradient slope correction.
4. Evaluate the transient analytical solution upstream from outlets through
   the river trees.
5. Add the hillslope advection term and re-evaluate samples that exceed the
   thermal critical slope.
6. Relax toward the new elevation and repeat before jittered bilinear
   upsampling to the next level.

After the finest level, constrained altitude correction removes drainage-basin
cliffs in receiver-difference space. This is not a global blur: it preserves
nonnegative downstream drops and the solved river trees.

The process graph is distinct from `compute_regional_hydrology`. The existing
fractional hydrology remains a common post-generation diagnostic over final
`height_m`; replacing that public diagnostic contract is not part of this
candidate.

## Product Fields

For the landscape-evolution recipe:

- `source_height_m` is the broad initial terrain;
- `uplift_potential` remains the normalized procedural driver;
- `uplift_rate_m_per_year` is its physical model input;
- `process_drainage_area_m2` and `process_flow_direction_x/z` describe the
  single-receiver process graph;
- `fluvial_advection_rate_m_per_year` and
  `hillslope_advection_rate_m_per_year` expose the two analytical speed terms;
- `thermal_active_mask` identifies samples re-evaluated with thermal erosion;
- `analytical_height_m` is the multigrid solution before altitude correction;
- `altitude_correction_delta_m` is the constrained correction only;
- `process_delta_m` is final height minus source height;
- `height_m` is the corrected regional macro terrain.

The recipe is finite regional truth and is explicitly not independently
patch-seam-safe. Later streaming must cache and extract patches from a shared
regional solve rather than solving each visible tile independently.

## Oracle Gate

The optional oracle runner consumes Cubey's lossless `source_height_m` and
`uplift_potential` exports, invokes the external analytical reference, and
writes ignored captures plus provenance. The runner must record the reference
commit, input content hash, exact parameters, any power-of-two edge crop, and
output distributions.

Implementation proceeds past the oracle only when the central review region
shows continuous drainage, broad ridges separated by incised valleys, no
large drainage-basin cliffs, and no obvious cardinal or diagonal dominance.
Failure is documented; it must not be hidden with hand-authored masks.

## Acceptance

Across seeds `0`, `9012`, and `12345`, the Cubey candidate must have no
unresolved process sinks, basin-discontinuity coverage below `1%`, and
orientation anisotropy no greater than `1.5`. Final deep-fill coverage from the
common diagnostic hydrology must not regress from the broad control. For seed
`9012`, height and slope percentile profiles should remain within `25%` of the
oracle without requiring pixel identity.

Visual acceptance requires coherent mountain mass, branching ridges, incised
valleys, and readable medium-scale detail in both oblique and surface-low
views. Closed contour fins, stepped shoulders, isolated pasted peaks, and
square-domain watershed patterns are rejection conditions.

## Deferred Work

- multi-scale fluvial, thermal, and deposition amplification;
- fine residual fields and terrain LOD;
- GPU or parallel optimization;
- imported DEM/runtime field loading;
- visible water, materials, vegetation, and planet integration.

