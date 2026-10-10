# Fluid 2.5D opt-in steep-flow presentation spike

2026-10-09. Partial visual improvement, not a ribbon/topology fix. All presets keep
rapid strength zero. Solver, terrain, wet support and reconstruction are unchanged.

## Controls and scope

Scenic material v2 JSON adds `water_rapid_strength` (0..1, default 0) and
`water_rapid_scale_m` (64..384 m, default 192). The latter is a procedural texture
period, not a physical wavelength. GUI: Water appearance and daylight → Steep-flow
rapid material / Rapid texture period.

Activity uses filtered velocity and the unperturbed surface normal, undoing display
height exaggeration. Speed fades over 0.08..1.4 m/s, downhill grade over 0.2..1.0,
wet support over max(wet threshold, 0.002)..max(threshold+0.001, 0.08) m. These are
artist thresholds, not calibrated turbulence. Flat-fast, cross-slope, uphill and
stationary surfaces are excluded from this specific steep-flow mask.

The material raises local roughness toward 0.52 and replaces at most 25% of clear
water shading with patchy sun/SH-lit diffuse appearance. Existing procedural detail
and two offset 8 s phases provide bounded flow mapping, capped at 8 m/s. It uses
the real-time presentation clock, not the old slowed decorative-normal clock; the
existing pause/seek/reset behavior and 1024 s wrap are retained. This is not
transported foam, a new simulation field, displaced geometry or narrower channels.

One vec4 is appended to the shared uniform: 368 bytes, offset 352. No new resource,
pass, mesh or texture. Three extra filtered texture samples in the active shaded
mode; calm pixels still pay that sampling cost when the option is enabled. Off
skips the new work. Existing terrain scalar noise is only a prototype pattern.

Water views append slots 12 `rapid-activity`, 13 `rapid-foam`, 14 `no-rapid-foam`.
Activity is independent of artist strength; foam shows the applied weight. The
last view is a full-shading roughness-only ablation, honoring the artist coverage
fade. Slots 1..11 retain the clear-water/field diagnostics and ignore rapid material.
All diagnostic colors still use Scenic exposure/tonemapping, not linear readback.

## Verdict and evidence

The first aggressive mix looked chalky/mottled and was rejected. The retained
restrained treatment reduces the left cascade's polish, but changes the middle
and right ribbons much less. The continuous footprint remains. No additional
normal band was added and no default was promoted. Human GUI acceptance is deferred.

Report: `outputs/fluid/steep-flow-v1-20261009-oT2Jye/index.html`. Baseline, mask,
rejected strong and retained stills are separately pinned. Numerical recordings
are immutable; each capture validates upload and reports zero hydraulic dispatches.
The report includes held/advancing clips and alternating render-only cost results.

Validation: 190 retained plus 96 activity and 16 phase GPU arithmetic controls;
18 full-renderer synthetic captures; material bounds/round-trip tests, five focused
CTests plus the recording contract and registered synthetic GPU CTest, Python
compilation, formatting and diff checks. Synthetic inactive shaded
cases and coverage/depth/direct diagnostics are exact. Automated checks are not
proof that all three marked regions now look natural.

Held-field motion shows small encoded adjacent-frame changes; advancing clips are
explicit 300x saved-field replay, not live simulation. Phase helper/clock unit
checks do not replace GUI seek/reset or a rasterized wrap/reversal/dry-out stress
test; those remain evidence gaps. Render-only presentation p95 increased about
1.1% on the measured RTX 5070 Ti, not a whole-app or cross-GPU guarantee.

Reusable tools: `projects/fluid/fluid_25d/review_steep_flow_v1.py`,
`test_steep_flow_gpu.py`, `test_review_steep_flow_v1.py`. The GPU synthetic test is
archive-independent and registered as `fluid_25d_steep_flow_gpu`.
