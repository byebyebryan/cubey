# Coherent marble mineral color

Status: planned; implementation goal loop authorized on 2026-10-22.

## Problem and visual target

The guardian's Balanced marble has accepted vein density and a cream/original
palette. The first color treatment looked stained. Higher-frequency pockets
improved definition, but their shapes remain mostly independent noise, weakly
biased toward the veins. The main vein cores also suppress warm color.

Make the warm veins, surrounding cream and occasional pockets read as one
mineral structure. The target is predominantly pale stone, selected warm
vein segments, irregular cream shoulders and localized wider pockets, with
fine secondary wisps. Both warm and gray-only veins should occur. Color
continues through the carved volume and is independent of cavities, lighting
and construction state. This remains a stylized procedural material.

References inspected during discussion:

- [Calacatta Cremo](https://www.antolini.com/en/c6/natural-stone-collection/m1556/calacatta-cremo):
  broken warm streaks follow the gray structure, leaving large pale intervals.
- [Calacatta Macchia Vecchia Original](https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original):
  warm vein edges widen into irregular pockets.
- [Antolini's marble overview](https://www.antolini.com/en/natural-stones/marble)
  describes mineral impurities in the swirls and veins.
- [PBRT's marble texture](https://www.pbr-book.org/3ed-2018/Texture/Noise)
  organizes color around a noise-perturbed strata coordinate.

The visual interpretation above is an art direction for this piece, not a
geological simulation or a claim that every marble follows the same pattern.

## Implementation boundary

1. Reuse the existing warped main and secondary vein coordinates. The main
   coordinate determines both a colored core and its surrounding mineral band.
   Fine secondary veins can carry warm color along their existing paths.
2. Use seeded noise to select warm segments and vary their width, edges and
   intensity. Occasional wider deposits form pockets attached to this same
   structure. Noise does not independently create cream islands across the
   remaining surface.
3. Retain the original pale/cream multipliers and the existing 0–0.25 amount
   control, saved default 0.08. Color can reach the vein cores. Native review
   decides widths, breakup and relative core/shoulder strength.
4. Broaden narrow color boundaries using analytic gradients and projected
   pixel footprints; fade unresolved warm structure. CPU and GLSL agree.

Keep the accepted neutral marble bit-exact at amount zero. Retain the four
detail choices, explicit scene parameters, the 80-byte pattern record,
limestone, roughness, bump, geometric tracing normals, model coordinates,
material ownership, geometry/history identities and one-build material edits.
No additional UI control, serialized parameter, image map, displacement,
lighting feature or authoring system is required for this pass.

## Checkpoints and local commits

1. Commit this plan and preserve the current comparison as the before image.
2. Implement matching CPU/GLSL color evaluation. Inspect bounded native trials
   at default and stronger amounts in shaded, base-color, close-up and rear
   views. Reject disconnected blobs, uniform ribbons, a general yellow wash,
   high-frequency speckles or a pattern that obscures the face. Commit the
   implementation only after focused CPU and native checks pass.
3. Capture the selected result with matching neutral/default/stronger views
   and turntables. Keep the previous renderer's receipt for old images; compare
   equal scene parameters, cameras and lights. Measure tint-off/on 1080p GPU
   cost without competing owned GPU work.
4. Publish a compact review through the existing report workflow, including
   references, native captures, exact artifact identities and scoped receipts.
   Verify served bytes and available browser interactions, document limits and
   commit the evidence. All commits remain local; no push is authorized here.

## Acceptance and limits

- Visual inspection: warm cores, shoulders, pockets and wisps share a readable
  flow; pale areas remain; default and stronger amounts can be judged in native
  views and motion. Human artistic acceptance remains with the user.
- CPU tests: warm vein cores occur in a controlled unwarped field; points well
  between main and secondary bands retain the pale material. Retain bounded,
  finite, one-sided cream color and unchanged roughness/gradients across filter
  footprints. These test spatial/color contracts, not preferred artistic values.
- Native checks: preserve existing procedural color tolerance 0.0002 and
  geometry/gradient/normal tolerance 0.00002. Retain plain/limestone, neutral
  marble, deterministic seed, shortcut/full, ownership, save/reload, short
  cut-history and material-cache checks. Exercise colored detail extremes.
- Presentation: pair old/new images at the same amount, distinguish technical
  passes from visual judgment, and identify any unavailable browser review.
- A color-only revision does not require replaying every guardian operation
  or rerunning unchanged desktop controls. Retain their earlier receipt scope.
- A successful loop delivers the planned implementation and review evidence;
  it does not establish a photorealistic fit or user artistic approval.

Progress and final receipts will be recorded in the
[procedural material evidence](sdf-procedural-materials-evidence.md).
