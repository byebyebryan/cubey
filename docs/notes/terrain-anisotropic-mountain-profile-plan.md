# Terrain Anisotropic Mountain Profile Plan

Date: 2026-07-01

Revision 26 fixed the earlier stacked-feature failure: visible mountain height now
comes from `mountain_profile_height_m` plus bounded detail instead of separately
pasted ridge, peak, and uplift layers. The remaining failure is the source shape.
The mountain stress recipe is coherent, but still too isotropic: ridge influence
reads as softened straight bands and summit support reads as round blobs.

## Target

Keep the coherent height rule, but replace the source primitives:

- ridge support should come from curved crest fields with variable width and
  flank falloff, not direct peak-to-peak segments;
- summit support should be elongated along connected crest direction, not painted
  as discs around anchors;
- highland mass without nearby crest or summit support should become saddle /
  negative-space suppression rather than inflated mountain pillows;
- `mountain_profile_height_m` remains the source height, while ridge, summit,
  shoulder, and uplift fields explain the profile instead of stacking visible
  layers on top of it.

## Review Order

For the revision 27 mountain stress pass, inspect these together:

1. `mountain-ridge-influence.png`
2. `mountain-summit-core.png`
3. `mountain-saddle-gate.png`
4. `mountain-profile-height.png`
5. `mountain-perspective.png`

The target read is gradual mountain buildup with attached crests, saddles between
high structures, and summits that feel supported by a ridge system. This pass is
not expected to solve erosion, snow, vegetation, or biome material polish.

## Outcome

Revision 27 implements the target source pivot and regenerates local review
captures under `outputs/terrain/mountain-range-stress` and
`outputs/terrain/mountain-range-stress-1025`. The ridge and summit source views
now read as curved crests and elongated support rather than straight bands and
round discs. The perspective render is directionally better, but still not a
finished mountain model: crests and summit lobes remain visibly synthetic because
they are shaped by source fields rather than erosion, talus, strata, or
world-scale range processes.
