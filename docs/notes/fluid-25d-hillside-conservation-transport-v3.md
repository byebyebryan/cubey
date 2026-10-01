# Hillside conservation and transport V3

## Scope

Continue the visually accepted immutable Terrain Diffusion hillside, not a new
site survey. Both the native-30m 256 and same-source 512 recipes, dry start,
continuous 100 m3/s input, dt 2 s / 16 substeps, gravity, damping, and outward-only
crop edges are unchanged. There is no rain, interior drain, erosion, prefill,
terrain carving, or source-rate retuning. `virtual-pipes` remains the default;
this study is explicitly opt-in `finite-volume`.

Evidence root: `outputs/fluid/hillside-conservation-v3-20260928-SlPlGa/`.
The [V2 failure](fluid-25d-hillside-sustained-flow-v2.md) and its diagnostic-only
captures remain historical evidence; this pass does not rewrite them.

## Diagnosis before the fix

An opt-in, committed-update audit records source/sink field changes, signed
internal-face transport, boundary transport, update arithmetic, clamping, and
independently accumulated source/sink/export deltas. The audit is observational:
it never corrects, redistributes, or feeds water back into the solve. A candidate
audit is accumulated only after a globally accepted substep; rejected updates
and explicit reset preserve the existing transactional boundary.

The instrumented old 256 solve reproduced all **414,000 shared historical metric
values** over two hours at six-decimal CSV precision. Its measured field budget
closed within 0.000005 m3. The final -372.251741 m3 residual decomposed into:

| Contribution to the signed residual | m3 |
| --- | ---: |
| Represented source addition minus independent source ledger | +1.886569 |
| Internal-face transport imbalance | +0.009671 |
| Boundary ledger definition minus transported boundary water | -0.010332 |
| Published-field update arithmetic | **-379.887775** |
| Clamp correction | 0 |
| Cumulative ledger rounding | +5.750131 |

Thus boundary accumulation alone was not the cause. Closed uniform-source and
closed spreading controls independently exposed discarded source increments
and update-rounding drift without an open perimeter. Simply preventing compiler
contraction of the four source/update assignments did **not** fix either the
long water gate or the old long tracer gate. Those failed prototypes remain in
the evidence root and are not the implementation being accepted.

The unchanged Site-A dye control also had a reconciled budget: -0.003202 m3
source representation, +0.000015 internal transport, -0.000224 boundary
definition, -0.001028 update arithmetic, and -0.001795 cumulative ledger
rounding explained its -0.006234 m3 final tracer residual. Zero explicit sinks
did not make this remaining discrepancy disappear.

## Narrow numerical change

Finite-volume conserved depth and tracer q now retain a local float high/low
remainder between accepted updates. Source addition, face-flux accumulation,
transport scaling, and the cumulative water/tracer ledgers use compensated
arithmetic. The existing float high fields still drive rendering, face fluxes,
momentum, and diagnostics. This is not a water-table adjustment, a global mass
correction, or movement of water between cells to balance a ledger. It does not
require GPU float64. Reset clears the remainders; a rejected substep publishes
neither them nor its ledger/audit deltas. Prospective values, including low
remainders, are validated before commit.

**All existing comparison/conservation tolerances remain unchanged.** The GPU
fix first passed the old long Site-A strict tracer control, but the original
CPU reference failed the hillside's 1800-second depth comparison: 0.001590 m
versus the existing 0.000500 m limit. GPU water residual was +0.004675 m3,
whereas the reference had lost 23.250823 m3. The already-running 7200-second
comparison was stopped at this decisive failed shorter gate; its exit-143
receipt is retained, not counted as a completed comparison.

A separate CPU-only audit of the original Site-A reference measured -42.824119
m3 water drift and -0.002728 m3 tracer drift at two hours. Independent closed
controls also exposed source representation and float-publication drift. The
reference now retains discarded water-depth increments in local **double**
remainders and measures the configured source delta rather than subtracting
rounded depth fields. Its existing once-per-face double flux evaluation,
float published depth/momentum, tracer arithmetic, forcing and tolerances are
otherwise retained. This is independent reference arithmetic, not copying the
GPU's compensated helper or applying a global budget correction. Reset clears
the remainders, and rejected updates do not publish them.

In the CPU-only controls, Site-A water drift fell to -0.000268 m3, the closed
spreading residual to +0.000000838 m3, and the uniform source integral returned
to its configured value. The original reference had added 520.087409 m3 extra
water in that source control. CPU-only dimensions/forcing differ from GPU
diagnostic controls: these magnitudes are not a matched solver comparison.
The reference skips flux arithmetic only for exactly zero-depth,
zero-momentum, zero-tracer faces; it does not classify positive films as dry.
All four retained control CSVs are byte-identical with and without that exact
zero shortcut. Final strict comparisons must still pass on the aligned build.

`--fluid25d-mass-audit` is headless finite-volume profile-diagnostics only.
`--fluid25d-mass-audit-control source-basin|dam-break` is an explicit headless
dry-bed diagnostic override, not a new normal scenario or default. Audits and
full-field diagnostic readback timings are not normal interactive performance.

## Evidence gates and historical comparison

The older V2 runner requires a bit-identical first-30-minute CSV prefix. The
conservation fix intentionally changes the published floating-point state, so
`final-256/hydraulics.json` retains an overall V2 rejection even though its child
exits successfully, all 3,600 conservation/status checks pass, and the measured
edge trigger still occurs at 2328 s. Its old-prefix mismatch is retained, not
renamed a pass.

V3 acceptance is a numerical-revision gate: frozen input/forcing identity,
successful original child, complete every-step cadence, the **unchanged** water
and tracer tolerances, strict oracle checks, and explicitly retained historical
differences. A 512-domain comparison is justified by the healthy 256 edge
encounter; the larger domain need not itself encounter an edge to be captured.
This is not a general permission to accept an unhealthy V2 report.

Profile diagnostics additionally fingerprint the complete published hydraulic
state and its conserved water remainders, excluding dye. The FNV-1a-style 64-bit
fingerprint is serialized as two exact uint32 metrics. Matching pulse/no-pulse
fingerprints plus all hydraulic metrics are a deterministic replay check, not
a scientific hydrology certificate or a cryptographic proof.

On the aligned build, the strict 60/600/1800-second hillside ladder passes.
The 1800-second maximum depth difference is 0.000014 m, within the unchanged
0.000500 m limit; reference water residual is +0.000153 m3. The old two-hour
Site-A strict dye control also passes again, with maximum depth difference
0.0000002 m and maximum tracer-concentration difference 0.0000001. Its 3600
profile frames independently pass the existing tracer conservation gate,
maximum absolute residual 0.000357 m3, with no explicit sinks or sticky flags.

The refreshed `aligned-256/hydraulics.json` has the same prefix-only V2
rejection, successful child and passing numerical checks. Its complete metrics
CSV is **byte-identical** to the preceding GPU-only candidate, including all
hydraulic fingerprints. The observational audit therefore remains applicable
across the subsequent CPU-only reference change: the complete compiled shader
map is unchanged. This is a measured replay bridge, not a claim that the audit
itself ran with the later app hash.

Both unchanged two-hour hydraulic profiles pass every-frame accounting and
status checks. The wider crop uses the same physical source, not finer cells:

| Native-30m domain | Stored water at 7200 s, m3 | Perimeter export, m3 | Maximum absolute water residual, m3 |
| --- | ---: | ---: | ---: |
| 256x256 | 521087.284640 | 198912.725384 | 0.013719 |
| 512x512 | 720000.017620 | 0 | 0.018935 |

Both source ledgers are 720000 m3 and explicit sinks are zero. In 512, material
water reaches 5219 m from the source over a maximum wetted-bed drop of 916 m,
while staying at least 1260 m from the crop edge. That is why requiring the
**larger** crop to encounter its own edge would be the wrong capture gate.
These are wet-cell observations, not parcel speed or routed path length.

The matched 512 dye profile passes independently recomputed tracer accounting
at all 3600 frames: maximum absolute residual 0.000459 m3, pulse integral
12000 m3, zero explicit sinks. All **374400 non-tracer metric values**, including
both hydraulic hash words per frame, exactly match the no-dye solve. At 75
minutes the amount-weighted centroid is 8.424 cells (253 m straight-line
displacement) from its first dyed observation; peak material dye spread is
1157 wet cells. The comparison does not impose an expected direction or infer
a parcel trajectory from that centroid.

The final **strict 7200-second 256 hillside comparison passes**, with zero
status flags. Maximum errors are 0.0000160 m depth (limit 0.000500 m),
0.0000067 m2/s momentum (limit 0.002), and 0.0000081 m/s velocity (limit
0.002). GPU water residual is +0.010024 m3 and CPU reference residual is
+0.000902 m3. Its entire metrics CSV is byte-identical to the no-oracle
256 hydraulic profile: running the reference did not change the GPU solve.
These are strict final **end-state** samples, not all-substep CPU/GPU parity,
and there is no two-hour strict 512-domain claim.

All three matched two-hour videos and five physical-time stills completed
successfully, retaining both raw and labelled artifacts. Each full video is
1280x720, 30 fps, 3600 frames and 120 seconds: **60x physical-time playback**.
The labelled close-oblique excerpt covers physical minutes 55-85 in 30 seconds
without resetting its encoded simulation clock. `transport/report.json`
revalidates commands, strict receipts, input pins and all 16 canonical media
artifacts; `media-derivatives.json` separately records the excerpt and exact
review-frame extraction commands/hashes.

Sampled close-oblique frames at 60/62/75/85 minutes show the useful story:
the source first turns magenta, then becomes clear blue again while dyed water
spreads into the broad storage area and extends along the lower-left branch.
The wide overview is useful for terrain context but too distant for reading
the early water/dye front. Prefer the close-oblique excerpt; use the top-down
movie to inspect branching. The native-30m shoreline still exposes grid steps,
and broad dye spreading includes storage/mixing and first-order numerical
diffusion. This is sampled-frame/format QA, not a claim of live human visual
acceptance or a fully reviewed animation/performance benchmark.

## Reproducibility boundary

The integrated app is pinned to SHA-256
`4113f2ba9c406c4c5163666a8cc6b7a4fb2b39ed531ffeebafe42b5627f60df6`;
the complete 24-module compiled shader map is
`c197ca1d437e012f5813c308eca24b45d34c7e57cd5df47cf77293e331da26bd`.
The aligned execution receipts record the exact commands, successful exits,
before/after app/module hashes, and hashes of their logs, profiles and PNGs.
V2/V3 additionally verify both recipes, terrain manifest, full elevation and
transformed-crop identities. The cache and retained old baseline CSV are
prerequisites; these runners are not a terrain-download workflow.

Historical executable/shader snapshots are retained identity evidence, **not
self-contained replay packages**: the executable embeds its build shader path.
Do not launch an old executable against newer, potentially ABI-incompatible
modules. A rebuilt app must receive its own matching receipts rather than
inherit these strict results by filename. Historical failed/cancelled runs and
the intentionally mismatched annotation-only smoke test remain separate from
the accepted `aligned-*` evidence and actual `transport/` media.

Integrated validation on the aligned build passed all 135 CTests, including
the real-sink/zero-sink tracer lanes and GPU CFL rejection. All 139 project
Python controls passed, including the V3 failure-path/media-policy tests and
the explicit 512 capture-gate policy cases. These automated checks do not
substitute for the user's later live visual acceptance.
The final app rebuild was a no-op and preserved its SHA-256, confirming that
the inspected source and validated local executable remain aligned. This pass
leaves the scoped worktree changes **uncommitted** and does not push.

## Reading the transport experiment

The optional pulse colors the continuously supplied water from physical time
3600 through 3720 s (60-62 minutes), injecting 12,000 m3 of tracer equivalent
at concentration 1. Clear supply continues afterward. Magenta/violet is
**concentration**, not depth, velocity, lighting, foam, or a decorative surface
animation. The pulse follows the same accepted face fluxes as water. It is a
transport marker in an artificial-source generated landscape, not evidence of
a naturally mapped river.

The green ring is the only chosen endpoint: the five-cell continuous source.
There is no amber drain. Any export is through the outward-only crop perimeter.
Centroid displacement is a parcel-position observation, not a streamline or
travelled path length; dyed-cell spread uses the existing material-concentration
threshold. Slow dye in a basin may indicate storage/mixing rather than a stalled
solver. Numerical diffusion remains a first-order shallow-water limitation.

The continuous GUI launcher retains its original water-only 256 default and
has no timed pause or finite frame limit. The wider pulse is explicit:

```bash
rtk proxy python3 projects/fluid/fluid_25d/run_hillside_flow_demo.py \
  --domain 512 --dye --view transport-inspection --camera overview
```

Space pauses/resumes; R resets to dry start and resets the pulse clock. At 8x
windowed playback, the pulse still occurs at 60 **simulated** minutes, not
immediately on opening. The source continues after the pulse and after the
bounded two-hour evidence horizon; the launcher does not auto-pause there.
