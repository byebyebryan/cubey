# Procedural Milky Way V2 Research

Status: implementation checkpoint. The runtime default is now
`atmosphere-night-sky-atlas-v2`; `v1` remains available through
`--milky-way-formula v1` for comparison. This note captures the motivation,
baseline, and acceptance criteria behind the replacement.

## Current Cubey Baseline

The active night-sky atlas is generated on the CPU by
`cubey::render::generate_night_sky_atlas` and sampled by the shared atmosphere
shader. It already has useful diagnostic layers:

- `final`
- `stellar-emission`
- `dust-tau`
- `star-clouds`
- `hii-emission`
- `speckles`

The current generator has the right broad decomposition: stellar emission,
dust extinction, star-cloud brightness, small H II color accents, and compact
speckles. The weak part is how some of that structure is authored. It uses
local value-noise helpers plus hard-coded `SkyStamp` arrays for bright knots,
dust lanes, and H II regions. That is acceptable as a v1 art approximation, but
it conflicts with the procedural-foundation direction: the Milky Way should
come from coherent fields and operators rather than hand-placed landmarks.

The moon is in a better place. The visible lunar surface map already uses the
shared procedural noise/seed/hash helpers and keeps deterministic artifact
metadata. The main cleanup target is the Milky Way atlas, not the moon.

Current capture baseline:

- `outputs/atmosphere-milky-way-layers-current/contact-sheet.png`
- `final` and `stellar-emission` read as a very broad smooth band with a bright
  center. That is usable as a low-contrast surface-sky backdrop, but not a
  strong procedural Milky Way diagnostic.
- `dust-tau` mostly reads as another thick centered band. V2 needs dust lanes
  and filaments that carve the emission field instead of only widening the
  band.
- `star-clouds` is closer to the desired structure, but the detail is still
  locked to the same broad symmetry.
- `hii-emission` reveals the current problem most clearly: warm regions are
  visible as a handful of ellipse stamps.
- `speckles` is a reasonable separate fine-grain layer, but it should stay a
  supporting term and not compensate for missing star-cloud structure.

## Reference Takeaways

Astronomy references point to a layered model:

- The Milky Way should be a broad disk with a brighter inner bulge/bar and a
  thinner central band.
- Dust is primarily an extinction field in visible light. It blocks background
  stellar emission and creates dark lanes; it should not be treated as another
  bright texture layer.
- Dust structure has rich filaments and cloud complexes, not just one smooth
  stripe.
- H II/star-forming regions belong close to the galactic disk and correlate
  with young massive stars. They should be sparse, warm accents, not a generic
  red wash.
- The actual Galaxy structure is irregular. Perfect logarithmic arms or
  symmetric stamps read artificial from an Earth sky view.

Local shader references agree with the operator direction:

- `~/code/ref/FrameGraph-Samples/.../IveSeen.glsl` uses a simple galactic band,
  noise, dark masking, and several star-density scales. It is not physically
  complete, but the useful lesson is to subtract dust/dark-lane structure from
  an emission band instead of painting all detail as light.
- `~/code/ref/3DWorld/shaders/nebula.frag` shows useful volume/nebula shaping
  operators: radial attenuation, multi-octave texture/noise, ridged noise, and
  alpha edge falloff.
- `~/code/ref/ShaderToy/starry_night_*` is useful for foreground stars and
  sky-noise discipline, but it is cloud-focused and does not provide a strong
  Milky Way model.
- Existing Cubey shader/procedural foundation already has CPU coherent noise,
  domain warp, legacy deterministic value noise, shared seed derivation, and
  GLSL procedural includes. V2 should reuse those instead of keeping a private
  noise implementation in the atlas generator.

## V2 Model Target

The v2 generator should keep the same atlas product and layer diagnostics, but
replace hand stamps with a deterministic field recipe:

```text
galactic coordinates
  -> stellar disk + bulge/bar emission
  -> procedural arm/spur probability field
  -> dust optical-depth field with warp/ridges/cellular filaments
  -> star-cloud field gated by stellar disk and eroded by dust
  -> H II candidate field from sparse procedural maxima gated by star-clouds
  -> foreground speckles/bright stars remain separate from the atlas
```

Recommended source fields:

- `disk_density`: latitude falloff, longitude-dependent width, and broad
  center/anticenter weighting.
- `bulge_density`: asymmetric inner bulge/bar around the galactic center.
- `arm_phase`: low-frequency spiral/spur probability. This should be
  irregular and optional, not a perfect arm drawing.
- `dust_tau`: ridged FBM plus domain-warped cellular/filament operators, gated
  to the disk and biased toward the inner band. Dust applies RGB extinction to
  stellar emission.
- `star_cloud_density`: thresholded/eroded high-frequency detail from the
  stellar field, using dust and arm probability to break it up.
- `hii_density`: sparse procedural cell maxima gated by star-cloud density and
  disk density, with warm color and low strength.

Recommended operators:

- domain warp before both dust and star-cloud detail;
- ridged noise for dark lanes and filaments;
- cellular/Worley distance or local maxima for sparse H II candidates;
- quantile/remap shaping for layer stability across seeds;
- optional derivative-aware or mip-aware filtering only after the layer
  structure is right.

## Implementation Outcome

Completed:

1. V1 and V2 formulas are selectable with `--milky-way-formula v1|v2`.
2. V2 uses shared procedural seeds and shared FBM/ridged noise helpers instead
   of hand-authored landmark stamps.
3. The atlas product, shader sampling path, and diagnostic layer names are
   unchanged.
4. `projects/atmosphere/capture_milky_way_layers.sh` defaults to V2 and accepts
   `FORMULA=v1` for direct comparison.
5. Focused tests cover formula parsing, metadata formula versions,
   deterministic output, layer distinctness, and non-empty V2 dust/H II layers.

## Acceptance Criteria

- No hand-authored `SkyStamp`-style landmark arrays in the v2 formula.
- The `dust-tau` diagnostic shows coherent dark-lane/filament structure rather
  than isolated ellipses.
- `star-clouds` contributes fine clumping without turning the whole galactic
  band into noise.
- `hii-emission` is sparse, warm, and visibly tied to the bright star-cloud
  regions.
- Final output reads better than v1 in a dark-sky debug view and remains subtle
  under normal surface exposure, moon washout, and light-pollution controls.

## Sources

- ESA Gaia, "Fly through Gaia's 3D map of stellar nurseries":
  <https://www.esa.int/Science_Exploration/Space_Science/Gaia/Fly_through_Gaia_s_3D_map_of_stellar_nurseries>
- NASA/JPL Webb Sagittarius B2 article:
  <https://www.jpl.nasa.gov/news/nasas-webb-explores-largest-star-forming-cloud-in-milky-way/>
- Green et al., "A Three-Dimensional Map of Milky-Way Dust":
  <https://arxiv.org/abs/1507.01005>
- Ashima/stegu WebGL noise notes:
  <https://github.com/ashima/webgl-noise>
- Tuxalin tileable procedural shader operators:
  <https://github.com/tuxalin/procedural-tileable-shaders>
