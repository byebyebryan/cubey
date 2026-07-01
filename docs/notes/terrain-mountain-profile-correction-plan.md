# Terrain Mountain Profile Correction Plan

Date: 2026-07-01

Revision 27 moved the mountain stress recipe in the right direction by making
`mountain_profile_height_m` the coherent source of visible height and by
separating broad ridge influence from narrow crest diagnostics. The 3D preview
made the next failure mode clearer: the resulting mountains still read as
synthetic forms rather than coherent ranges.

## Current Failure

The revision 27 mountain stress capture has three visible regressions:

- some summits read as rounded bulges sitting on the range instead of peaks
  supported by a broader profile;
- ridge crests are too thin relative to their surrounding body, so they read as
  fins or blades when viewed in perspective;
- shoulder and saddle transitions still form stepped shelves in places instead
  of a gradual climb from foothill to high ridge.

These are source/profile failures, not renderer, material, erosion, or biome
polish failures.

## Revision 28 Target

The correction should keep the coherent height rule:

```text
height_m = mountain_profile_height_m + bounded residual detail/process effects
```

Within that rule:

- `mountain_ridge_influence` is the broad ridge body and flank support;
- `mountain_ridge_skeleton` is a narrow crest diagnostic, not the visible
  mountain blade;
- `mountain_summit_core` is sparse summit sharpening support, not standalone
  peak mass;
- `mountain_saddle_gate` remains gentle negative space between supported
  structures, not a hard contour cutoff.

The profile solve should therefore put most visible height into broad mass,
ridge body, and continuous shoulder ramps. Crest and summit fields may sharpen
supported high areas, but should not be able to create isolated spikes, fins, or
inflated pillows on their own.

## Review Order

For revision 28, inspect these files together:

1. `outputs/terrain/mountain-range-stress/mountain-profile-height.png`
2. `outputs/terrain/mountain-range-stress/mountain-ridge-influence.png`
3. `outputs/terrain/mountain-range-stress/mountain-ridge-skeleton.png`
4. `outputs/terrain/mountain-range-stress/mountain-summit-core.png`
5. `outputs/terrain/mountain-range-stress/mountain-perspective.png`
6. `outputs/terrain/mountain-range-stress/mountain-height-perspective.png`
7. `outputs/terrain/mountain-range-stress-1025/mountain-profile-height.png`

The expected read is not a finished alpine model. This pass is specifically
about making the source profile less blobby, less fin-like, and less shelfy
before larger erosion, talus, snow, vegetation, or biome composition work.

## Outcome

Revision 28 broadens `mountain_ridge_influence` while keeping
`mountain_ridge_skeleton` narrow, then retunes the coherent profile so broad
mass and ridge body carry more height than the crest and summit diagnostics. It
also softens `mountain_mass`, `mountain_shoulder`, and `mountain_saddle_gate` so
the profile has fewer hard shelf transitions.

The regenerated 513 capture reports generator revision 28, 52 fields, 46 scalar
outputs, `height_m.span = 1548.804`,
`mountain_profile_height_m.span = 1443.501`,
`mountain_ridge_influence.mean = 0.1600`,
`mountain_ridge_skeleton.mean = 0.0154`,
`mountain_shoulder.max = 0.6974`, and
`mountain_summit_core.mean = 0.0270`. The 1025 capture reports
`height_m.span = 1572.752`, `mountain_profile_height_m.span = 1507.812`, and
`mountain_ridge_influence.mean = 0.2247`.

The visual result is an incremental correction, not a solved mountain model.
`mountain-perspective.png` now reads less like thin fins over a plateau, but the
range still has rounded synthetic peaks and broad procedural shoulders. The next
meaningful improvement should come from a better mountain process model
(ridge/valley evolution, erosion, talus, snow/ice, or world-scale range graph),
not more local crest/summit height stacking.
