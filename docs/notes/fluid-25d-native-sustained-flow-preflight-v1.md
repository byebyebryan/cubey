# Native Terrain Diffusion sustained-flow preflight V1 — 2026-09-25

## Verdict

**Neither of the two audited sites is selected for the first dry-start,
continuous-source flow pilot.** This closes the raw-terrain gate before changing
the Fluid app. It does not mean Terrain Diffusion lacks rivers, that the solver
cannot move water here, or that a larger supply could never produce an overflow.

The intended showcase remains a continuously supplied stream forming on
unchanged terrain, followed by a dye pulse tracing that stream. A static drainage
line, prewetted corridor, interior sink, or blue lake-filling surface would not
establish that result. No new forcing protocol, source-rate matrix, water/dye
video, GUI scene, solver tuning, or default change was made in this checkpoint.

## Frozen audit scope

The [audit helper](../../projects/fluid/fluid_25d/native_sustained_flow_preflight_v1.py)
reads two existing, unmodified 2048 × 2048 Terrain Diffusion payloads at their
native **30 m** spacing. No seeds were generated and no terrain was carved,
smoothed, resampled, or filled for use by the simulation. Depression filling is
performed only on disposable analysis copies.

| Candidate | Raw elevation SHA-256 | Crop `(x,z,width,height)` | Source center `(x,z)` in full map |
| --- | --- | --- | --- |
| Rolling wet lowland | `fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2` | `(0,832,192,128)` | `(115,889)` |
| Temperate mountain valley reference | `a978ecd435d2a161598d78ecf71221cba53b371e98c3525d18ebeab6665aa737` | `(352,877,192,128)` | `(450,941)` |

Each proposed source footprint is a 3 × 3 native-cell patch. All four crop
perimeters are treated as open in the audit, matching the proposed outward-only
boundary contract. Expected exits are observations, not drains that force water
to follow a chosen line. The analysis records both raw input identity and the
exact row-major transformed float32 crop hash.

Full-map priority-flood/D8 contributing area provides a **locator**, not a flow
certificate. The new checks use cardinal/shared-face connectivity, raw bed
profiles, direct raw descent, source-patch elevations, and cropped four-neighbour
depression analysis. The short routed segments are about 1.2 km; the full
derived cardinal routes to the crop edges are longer (7.38 km and 4.56 km).
Those longer routes are not silently substituted for the requested short pilot.

## Findings

| Audit quantity | Lowland | Mountain reference |
| --- | ---: | ---: |
| Bed elevation range within 90 m source patch | 4.80 m | 55.68 m |
| Strictly downhill cardinal descent from lowest source cell before first pit/flat | 0 m | 90 m |
| Net fall along first 1.2 km of derived cardinal route | **−1.16 m** (net rise) | 33.82 m |
| Largest raw adverse single-cell rise on that route | 1.81 m | 12.04 m |
| Largest absolute raw one-step grade | 6.65% | 61.28% |
| Source-touching derived depression-component area | 1.908 km² | 0.7614 km² |
| Derived equilibrium fill-to-spill volume of that component | 2.677 million m³ | 11.672 million m³ |
| Maximum derived fill depth along the short cardinal route | 4.86 m | 30.32 m |

The lowland route starts within a substantial enclosed depression, rather than
on a clean downhill river reach. A source-first experiment there would need to
distinguish local pooling and basin filling from eventual downstream flow. That
is a valid future **pool/spill** experiment, but it is not yet a credible first
continuous-stream showcase under the bounded pilot contract.

The mountain reference is not the hoped-for moderate, well-resolved valley:
the source footprint spans very large bed differences and the raw route has
steep steps. Exploratory cross-sections at 0.5–1 m above their sampled floor are
mostly one native cell wide. The lowland sections at 0.5 m are often one or two
cells wide, becoming wider at deeper stages. These are resolution/suitability
warnings, not simulated wetted widths.

The section sampler searches a declared cross-axis neighbourhood and records
the floor's offset from the route. A floor may lie away from the routed cell;
successive minima are **not** assumed to form a connected channel. Section
integrals are geometric sensitivities, not a discharge calculation or a required
transient source volume.

For the complete nine-cell source patches, the lowest open-edge saddle is
identical in the four- and eight-neighbour minimax analyses for the lowland;
the mountain's cardinal saddle is about 0.18 m higher. Thus corner connectivity
is not the main obstacle found at these sites. The derived cardinal routes still use
filled/flat-resolved analysis copies; their existence does not remove the raw
depressions or prove a dry-start stream.

## Water-budget interpretation and review correction

The first draft incorrectly offered whole connected-sublevel volumes at one
source-to-edge minimax stage as possible spill budgets. That can raise downstream
valleys to the source's level even on an open downhill slope. **Those flat-stage
values (about 7.04 and 28.25 million m³) are not accepted as flow budgets.**

The reviewed method instead measures the positive per-cell fill delta of a
four-neighbour priority-flood copy and groups connected depression components
touching the source patch or short route. Both source-touching components here
have a constant derived spill level. The reported volumes are equilibrium
storage proxies, not proven lower bounds on mass needed for a transient front
or measurements of water currently present.

For scale only, supplying those proxies over two hours corresponds to roughly
372 and 1,621 m³/s. This division is **not** a recommended source rate, required
discharge, calibrated catchment yield, or arrival-time prediction. No common
`Q/2Q` rates were frozen, and no arbitrary high supply was chosen to manufacture
a site-selection pass. The earlier provisional 5/10 m³/s comparison was not
used as an acceptance threshold.

## Evidence and validation boundary

The final local replay lives under
`outputs/fluid/native-sustained-flow-preflight-20260925/final-replay/`:

- [Audit JSON](../../outputs/fluid/native-sustained-flow-preflight-20260925/final-replay/audit.json): identities, exact crop hashes, raw/derived route distinctions, source and depression measurements, synthetic controls, and a deterministic result fingerprint.
- [Raw-bed review](../../outputs/fluid/native-sustained-flow-preflight-20260925/final-independent-review/raw-bed-review.png): pink is the source-touching derived depression, green is the proposed source patch, and cyan is the short cardinal locator. In the lower profiles cyan is raw **bed elevation**, not water; yellow is the disposable analysis fill surface.
- [Independent check](../../outputs/fluid/native-sustained-flow-preflight-20260925/final-independent-review/independent-check.json): a separate plain heap-based cardinal boundary flood and source-component traversal exactly reproduce both storage proxies and component counts, verify raw/manifest/crop identities, and check the raw short-route profiles and cardinal, cycle-free traces. Repeating this crop-only flood produces identical arrays.

Earlier output directories are retained as draft evidence; the initial
`audit.json` is superseded and must not be used for a budget decision. Outputs
are local ignored artifacts, not versioned inputs or a promoted scene.

The new synthetic controls cover diagonal-only connectivity, known-bowl local
storage, zero depression storage on an open slope, shared multi-source storage,
cardinal route tracing, coordinate origins, and invalid input handling. The
existing hydrology-reader controls are also rerun. Independent deterministic
replay checks the cropped flood calculations; it is not CPU/GPU solver
parity or field hydrology validation.

Final validation: new preflight controls **9/9 passed**, existing hydrology
reader controls **4/4 passed**, final two-site replay completed with both crop
pins matching, independent repeated heap/BFS checks passed, and full dev CTest
**133/133 passed** (89.40 seconds). Python syntax and whitespace checks passed.
The Fluid app binary remained unchanged at SHA-256
`7e086345e1fb35fe92eb2a2039db7edeb28b394d759631c4ca96b3bc440de87f`.
Existing regression tests include GPU lanes; there is no new terrain-flow GPU
run or extended CPU/GPU parity result in this checkpoint.

Reproduce without overwriting an existing evidence directory:

```sh
rtk proxy uv run projects/fluid/fluid_25d/test_native_sustained_flow_preflight_v1.py
rtk proxy uv run projects/fluid/fluid_25d/test_terrain_hydro_read_v1.py
rtk proxy uv run projects/fluid/fluid_25d/native_sustained_flow_preflight_v1.py \
  --output-dir outputs/fluid/native-sustained-flow-preflight-v1-replay
```

The helper requires the existing cached heightfields and the prior pinned site
inventory at `outputs/fluid/terrain-site-scan-v1-20260923/scan.json`; their
identities are checked. This is an opt-in offline audit, not a startup dependency.

## Next decision

The useful next selection criterion is a **raw, shared-face downhill reach
outside a large enclosed depression**, with a source on a reasonably coherent
bed patch and enough channel width at native resolution. Apply that criterion
to the existing terrain before choosing more seeds or increasing supply. If a
pool/spill story is desired instead, declare that different outcome explicitly.
Neither direction is started by this checkpoint.

Existing authored headwaters, neutral rain/sheet studies, immutable terrain,
solver/oracle tolerances, and product defaults remain unchanged. The prior
near-dry long-run CPU/GPU velocity mismatch remains an open limitation; this
offline audit neither fixes nor extends that parity evidence.
