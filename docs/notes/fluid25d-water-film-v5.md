# Fluid 2.5D water-film presentation V5

## Current status: consolidated

The V5 study below is historical. After the owner reviewed the subtle change,
the wet-ground treatment was accepted as modest polish and folded into the
normal Scenic water shader. There is one water fragment and the existing
triangular/B-spline pipeline pair, not three film modes or extra study pipelines.
The `--water-film` and `--fluid25d-scenic-water-film` selectors are retired;
old invocations fail rather than silently select a different treatment.

Every Scenic material preset uses the same retained 2-12cm/.65/.75 treatment.
Bounded JSON controls and the two film material sliders remain. Water component
diagnostics remain independent of material selection. Readable remains the
launcher's default; solver, rain, terrain, bank reconstruction and coverage
are not changed. This is a small rendering refinement, not a solved realism
problem or a newly validated live hydraulic result.

The immutable V5 comparison media, study source, executable and SPIR-V remain
at `outputs/fluid/water-film-v5-20261007-study/`. The duplicate fragment and V5
comparison-only runner/tests/probe were removed from the active source tree;
they can be recovered from that bundle's `final-source` or the consolidation
bundle's `before-source`. V5's live-current verifier naturally no longer matches
the new default. Its artifact inventory remains unchanged.

Current controls use `test_scenic_water.py`, `test_scenic_water_gpu.py`,
`probe_scenic_water.py` and `review_scenic_water.py`. Matched migration evidence,
tests and timings are under `outputs/fluid/scenic-water-consolidation-20261007/`.
The normal shader preserves the selected candidate's arithmetic, support and
alpha; deeper-water branches remain inactive above 12cm. No tuning was added.

The subsequent commit review removed an accidental archived-recording dependency
from the synthetic GPU controls: they now pin the requested executable and its
sibling SPIR-V directory, and generate their own input fixtures. Native-scene
captures still pin the frozen rain recordings separately. The terrain/water
diagnostic receipts now agree when component views suppress dots. Post-review
validation is retained at `outputs/fluid/scenic-water-review-20261007/`; the
earlier sealed consolidation bundle is historical, not a live-current seal.

## Historical V5 study

This is an opt-in bare macro-landscape study, not a new solver or calibrated
wetness model. The retained Scenic shader is still the reference/default.
Numerical fields, rainfall, terrain, geometry, shoreline support and alpha
remain unchanged. Human visual acceptance is deferred; no default promotion.

## Study contract

`--fluid25d-scenic-water-film reference|rough-film|wet-ground` is independent
of the terrain material. The launcher forwards `--water-film` only when
explicitly selected with Scenic. Readable and refined remain its defaults.

Rough-film increases perceptual roughness, never decreases the reference
roughness. Wet-ground uses that rougher result and additionally blends the
complete HDR water shading toward the current wet terrain at the unshifted
screen coordinate. This avoids attenuating reflection alone while leaving
Fresnel transmission suppressed at grazing angles. Neither is a physically
complete layered wet-surface model. No persistent moisture/drying is implied.

The bounded optional material JSON properties retain the V2 schema:
`film_begin_m` (default .02), `film_end_m` (.12), `film_roughness` (.65),
`film_ground_mix` (.75). Begin must be strictly below end. The depth weight is
`1-smoothstep(begin,end,h)`, with `h` the existing render-supported physical
depth, never exaggerated displayed depth or optical ray length. At and above
end, the roughness/ground-blend branches are inactive. Discard and alpha code
is exactly the retained code; no hidden water-footprint expansion or cutoff.

These depths are a scene-specific visual audition. At recorded rain-off 6000s,
about 88% of cells are below 5cm, carrying about 12% of total water volume.
The initial 1–5cm/.55 roughness audition had only a small visual effect. The
2–12cm/.65 audition captures more of the direct-highlight transition areas
and is retained for opt-in comparisons. These are not a universal distinction
between ground and real open water; strong deeper-water highlights remain.

`--fluid25d-scenic-water-view` supplies environment-only, direct-only,
transmission-only, no-environment, no-direct, no-clarity, no-detail,
depth-bands, coverage and film-weight. Components retain the original alpha,
over the unchanged terrain, and suppress dots. Terrain diagnostics take
precedence in the GUI; conflicting startup component selections are rejected.

The study shader is a separate fork of the retained water fragment. It uses
the same optical/refraction guards, resources and geometry. The frame's
uniform prefix stays intact; two water-only vectors append after terrain V4.
Older shaders do not declare them. Water changes do not alter terrain bindings.

## Established references

- [Lagarde, wet-surface game approximations](https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/): wet substrate and accumulated free water are different appearances; practical material approximation is not a physical substitute for a layered model.
- [Filament material documentation](https://google.github.io/filament/main/filament.html): perceptual roughness and Fresnel/energy accounting. Cubey's GGX helpers square perceptual roughness internally, consistent with its generated environment/BRDF resources. The retained water uses premultiplied source-over (`ONE`, `ONE_MINUS_SRC_ALPHA`). No simple roughness-convention or blend mismatch was identified in this audit.

## Validation and review

The V5 runner freezes pre-pass sources/runtime/stills into a fresh output leaf,
captures matched final reference/candidates and raw controls, performs rotating
three-batch 720p/1080p profiles, and seals source/runtime/input/media hashes.
Synthetic GPU controls test dry/shallow/sloped/one-cell-stream/deep fields,
calm identical-state rendering, exact coverage across all three bank options,
and exact dry/deep parity. Those are renderer controls, not native hydraulics.

Review and measured verdict will be in the new `outputs/fluid/water-film-v5-*`
leaf's `index.html`, `RESULTS.md` and `acceptance.json`. Existing V4 artifacts
are not rewritten. Those statements describe the original study checkpoint,
before the accepted water treatment was consolidated and committed.
