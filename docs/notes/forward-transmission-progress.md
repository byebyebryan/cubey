# Forward transmission F0–F2 execution

Goal started 2026-11-07 in `cubey-raytracing-first-pass`, branch
`raytracing-first-pass-20261104`. Research recovery point: `7255cbc3`;
pre-renderer source: `87b7f615`.

Scope is the [research plan](forward-transmission-research-plan.md): prove an
explicit environment lookup, integrate it into the matched workshop and qualify
existing consumers. Local scene refraction, new rough BTDF filtering, GI and
cloud RT remain deferred. No push or public-site promotion.

## Checkpoints

- Baseline: five 1080p native forward captures retained under
  `outputs/raytracing/forward-transmission/f0-baseline`, with commands and hashes.
- F0 accepted with a disclosed ideal-sphere limitation: native direction-gradient
  mean error falls from .23495 to .003005 (about 78x), IOR-1 source difference
  p99 is 5.31e-5, and the constant environment matches exactly. Production
  arithmetic versus observed-normal controls passes .1-degree/.5% gates.
  Raw evidence: `outputs/raytracing/forward-transmission/f0-r4`.
- F1 implemented: typed source setting, CLI/UI/view persistence, explicit
  comparison presets and diagnostics. Existing consumers default to screen.
- F2 pending: consumer retention, lifecycle, media, costs and internal report.

## Decisions during execution

The shared recipe validator requires clear glass roughness .05. Keep that
contract; use a radiance-component diagnostic at mip zero to isolate lookup
direction without editing canonical recipes or weakening validation. Numerical
diagnostics execute the production fragment shader and export its pre-display
HDR image; PNG display quantization is not used to establish optical precision.

The existing RGBA8 neutral normal texture encodes (128,128,255), which creates
a shading-normal offset. The first native IOR-1 control measured ideal-sphere
Beer-length p99 error of .5366% of diameter, above the proposed .5% gate;
arithmetic with the observed shader normal agrees within 1.2e-6 of diameter.
Retain the original ideal-sphere gate result explicitly and add a conditional
oracle using the observed shader normal. This separates legacy normal/mesh
approximation from the lookup experiment, without changing normals or loosening
the .1-degree/.5% arithmetic gates. Ideal-sphere disagreement remains a disclosed
limitation, not a pass.

Raw diagnostics use an opt-in RGBA32F scene so readback quantization cannot
dominate the numerical gate. Ordinary rendering remains RGBA16F. The new copy
exposed a shared render-graph omission: transitions between different read-only
texture usages need layout barriers. Added a regression for sample → transfer
→ storage → sample, while consecutive reads in the same usage still omit work.

The completed F0 matrix measures a .3178-degree shading-normal tilt and about
.514-degree p99 ideal-sphere exit-direction error at IOR 1.5. These remain
reported failures of the original ideal-normal target. Observed-normal
arithmetic p99 direction error is at most .000288 degrees across the four IORs;
the two-pixel silhouette band excludes 1.238% of ideal sphere coverage.

## Historical retention investigation

The 291-test integration suite passed at `66e75372`. Its historical 640×480
forward bronze byte check then failed: one pixel differs by one code value.
Compiling the exact baseline shader reproduced its historical SPIR-V SHA256
`62e80fa4...`, but swapping that shader into the current runtime produced the
same current image as the new shader. The discrepancy is therefore not resolved
by restoring the historical fragment module. Its broader runtime cause is not
established, and the historical byte gate remains recorded as failed.

All five 1080p captures made immediately before implementation in this session
(gallery, clear glass, frosted glass, bronze and studio glass) remain byte-identical
in screen mode. Use these as the measured retention baseline for this pass;
retain the historical residual and shader-ablation evidence alongside them.
Do not overwrite historical captures or describe the historical byte check as
passed. Raw controls: `outputs/raytracing/forward-transmission/retention-analysis`.

The new optical diagnostic is contained inside the existing component-view
branch. F0 was rerun after this control-flow cleanup and retains the same
qualified optical results. No original lighting or material arithmetic was tuned.
