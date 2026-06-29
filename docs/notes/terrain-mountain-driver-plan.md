# Terrain Mountain Driver Plan

Date: 2026-06-29

Status: implemented in generator revision 19.

This note captures the first mountain-driver batch after the river-network
stress work. Before this pass, the revision 18 terrain product had coherent base
relief and `ridge_uplift`, but mountains were still mostly a height-noise
response. Revision 19 makes mountain structure inspectable as source fields
before adding glacial valleys, snow, lakes, or biome polish.

## Current Gap

The active height source is effectively:

```text
height_m = base_elevation + broad_relief + ridge_uplift + detail_residual
```

That is useful as a baseline, but it hides too many mountain decisions inside
one ridged field. There is no separate support field for where mountain ranges
exist, no peak support, no explicit broad uplift, and no debug view that explains
the difference between range mass, ridge line, and summit accent.

## Reference Lessons

- TerraForge3D is the most direct local reference for compact mountain base
  shapes, slope/height-gated detail, and field composition. Borrow the staging
  and controls, not the UI or shader runtime.
- ProceduralTerrains is useful for typed layer stacks: ridged, domain warp,
  terrace, flow, masks, and blend modes. Cubey should keep those ideas as C++
  recipe profiles and named product fields rather than a visual node graph.
- Planet-Generator reinforces first-layer masking: broad shape first, then
  ridges and details masked by that broad support.
- SoilMachine and SimpleHydrology are process vocabulary references for later
  talus, erodibility, discharge, and deposition fields. They are not first-batch
  hydraulic erosion dependencies.
- Large-scale uplift plus fluvial erosion is the right conceptual anchor for
  mountains and drainage, but revision 19 should stay deterministic and
  field-based instead of starting a full erosion simulator.
- Feature-curve diffusion is useful as a warning: ridge/river/cliff constraints
  can look good, but hand-authored lines would repeat the earlier canyon/ridge
  mistake. Any ridge-like constraints must be generated from coherent fields.

## Revision 19 Target

Add a new `temperate-mountain-range-stress` recipe that keeps existing river
recipes stable while exposing these fields:

- `mountain_support`: broad warped range mask.
- `ridge_support`: ridged structure inside mountain support.
- `peak_support`: high-percentile summit/peak accents derived from coherent
  ridge fields.
- `mountain_uplift`: broad range mass contribution to height.
- `ridge_uplift`: sharper ridge contribution, preserving the existing field name.
- `peak_uplift`: localized summit contribution.

The mountain stress recipe should use:

```text
height_m = base_elevation
         + broad_relief
         + mountain_uplift
         + ridge_uplift
         + peak_uplift
         + detail_residual
```

Routing should avoid fine detail and use a softer source:

```text
routing_height = base_elevation
               + broad_relief
               + mountain_uplift * 0.65
               + ridge_uplift * 0.55
               + peak_uplift * 0.35
```

The existing `temperate-mountain-river` and `temperate-mountain-river-stress`
recipes should keep their current visible terrain as much as possible: emit the
new fields, but leave `mountain_uplift` and `peak_uplift` at zero for those
recipes in this first implementation pass.

## Validation

- Product field tests should require the new mountain fields.
- Debug-view tests should parse the new field names.
- A mountain recipe test should assert useful but bounded support coverage,
  non-empty uplift fields, deterministic output, and meaningful relief.
- Existing river tests should still pass without retuning the default or stress
  river recipe.
- Review captures should include `outputs/terrain/mountain-range-stress` and
  `outputs/terrain/mountain-range-stress-1025`.

## Review Readability Correction

Revision 19 split the mountain source fields correctly, but `final.png` is still
the river/material debug composition. It mixes material masks, slope shade, and
the active river overlay, so the mountain stress recipe can look too close to the
river review even when the underlying support and uplift fields differ.

The follow-up capture batch adds a mountain-specific rendered debug view instead
of tuning the generator against `final.png`. The primary mountain inspection
image is `mountain-relief.png`, which should:

- remove river, wetness, vegetation, and material overlays;
- hillshade `height_m` with stronger relief contrast;
- tint broad height/range mass from `mountain_support`;
- highlight ridge and peak accents from `ridge_support` and `peak_support`;
- keep `final.png` unchanged for river/material review.

## Deferred

- Full hydraulic or thermal erosion simulation.
- Glacial valley shaping, snow/ice, treeline, and alpine material polish.
- Lake, wetland, coast, ocean, and standing-water products.
- GPU compute or shader-only terrain generation before the CPU/reference fields
  are credible.
