# Terrain Source v2.1 Refinement Plan

Date: 2026-07-13

## Decision

Terrain source v2 remains the immutable mountain-quality control. Source v2.1
is a new opt-in, mountain-only refinement that keeps v2's macro, structure,
detail fields, seeds, and elevation profile. It changes only how detail below
roughly 110 m contributes to final height.

The renderer, camera planner, materials, weathering, and default source remain
fixed. Source v1 stays the default. Source v3 remains available for diagnostics
but is not a promotion candidate.

## Why v3 Failed Visual Acceptance

Source v3 established a useful range/massif diagnostic hierarchy, but its final
composition is dominated by broad mass. Across the three review seeds, meso
RMS is less than one percent of massif RMS and ridge RMS is approximately seven
to eight percent. The result has substantial total relief while reading as
rounded bodies with broad shoulders in clay and midground views.

This is not primarily a framebuffer, tessellation, or footprint-filtering
problem. At the 64 m far-field footprint, the v3 ridge band is retained and
most of the meso band remains. The missing read is caused by weak, separately
capped mid-scale contributions after the massif profile has already been
formed. Recovering v3 would require a new ridge driver, valley driver, and
composition order; that would be a new source architecture rather than a
focused correction.

Useful v3 work is retained: named scale diagnostics, component reports,
domain-warp experiments, separate shader bundles, and fixed multi-seed review
packs.

## V2.1 Source Contract

V2.1 resolves the exact v2 mountain bands. Its detail wavelengths are
approximately 900, 443, 218, 108, 53, 26, 13, and 6 m. The first three octaves
remain in the nonlinear elevation composition. The remaining five octaves are
accumulated as a neutral-centered fine delta and applied after nonlinear
elevation shaping.

The implementation samples the eight octaves once. It preserves the complete
v2 detail value and separately accumulates the fine-tail deviation, then uses:

```text
core_detail = full_detail - fine_tail
core_height = v2_macro_structure_profile(core_detail)
fine_height = clamp(5500 * 0.16 * 0.5 * mass_gate * fine_tail, -30, 30)
height = max(base_height, core_height + fine_height)
```

The first fine-tail wavelength is approximately 108 m. At footprints of 64 m
or greater, that octave and every smaller octave are fully filtered to their
neutral values. Fine-tail deviation is therefore zero and v2.1 must reproduce
v2 geometry exactly.

## Acceptance

For seeds `0`, `9012`, and `12345`:

- existing v1 and v2 reports remain byte-identical;
- v2.1 equals v2 at 64, 256, and 1024 m footprints;
- unfiltered relief remains within 0.92 to 1.08 times v2;
- mean slope remains within 0.65 to 1.00 times v2;
- fine residual RMS, measured as `H(0) - H(64)`, remains between 0.35 and 0.90
  times v2;
- meso and structure residuals remain within 0.999 to 1.001 times v2;
- the matched far-field silhouette and mountain placement remain intact;
- top, oblique, and midground clay views reduce needle-like fine relief without
  rounded v3-style blobs or clipped terraces;
- matched quality/layered frame time remains below 33.3 ms and within 1.05 times
  v2.

This pass does not retune the 218 to 1200 m ridge structure. Any remaining
mid-scale fins are evidence for a separate refinement, not permission to widen
the v2.1 batch.
