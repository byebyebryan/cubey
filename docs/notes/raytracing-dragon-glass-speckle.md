# Glass dragon solar speckles

2026-11-16 investigation, after committing the smooth demo default in `2e86702`.
The earlier tracing repairs and experiments were preserved in `0ffde83`.
No speckle treatment or lighting default is changed by this investigation.

[Open the matched-image report](http://starship:8787/reports/f708b465ab998d9f25ef9e0090b34d00b95e514d163823badc044e604d38e891/).

**Subsequent research:** the
[established-solution comparison](raytracing-dragon-glass-noise-solutions.md)
supersedes this note's proposed next pass. It checks production clamp/denoiser
approaches and replays the bright paths before choosing a sampling strategy.
The measurements below remain unchanged.

## What the controls establish

Sixteen original full-HD captures retain the camera, sharp smooth glass,
marble pattern/relief and accepted mesh. Twelve ordinary-renderer controls
compare seeds 19/43 at 128/512/4096/8192 SPP and isolate sky/sun at 4096 SPP.
Four private previews broaden the shared solar angular radius fourfold at
128/512 SPP. The [evidence](../evidence/raytracing-dragon-glass-speckle-2026-11/README.md)
retains raw identities, diagnostics and reproduction scripts.

The small solar emitter drives the strong seed variation. In the fixed head
rectangle, combined HDR seed-difference RMS is .433731 at 4096 SPP, sun-only
.433702 and sky-only .001415. Sky-only has zero pixels with all three RGB8
channels at least 240. The combined seeds have 636/618 such pixels, with 179
in common. These rectangles include some background and are not segmentation
masks. Two seeds describe these realizations, not a population variance or
physical-accuracy measurement.

Private single-pixel logs reproduce ordinary HDR means within `5.90e-6`
relative RGB error at the selected pixels. At `(818,372)`, seed 19 averages
luminance 16.25, seed 43 .320. Three samples of about 21,750 luminance units
dominate seed 19; the median sample is .245. The bright terminal solar paths
follow five/six surface events. Seed 43 finds some solar paths, but their
Fresnel-weighted contributions are tiny and it finds no comparable bright
sample. Solar-hit counts alone therefore do not predict the visible outlier.

At `(738,527)`, the white highlight averages about 39 in both seeds. Nine
bright solar samples dominate seed 19. A generic sample clamp would also
attenuate this repeating feature. Repeating under two seeds is evidence of
stability in this comparison, rather than proof of convergence.

## Why more samples and a broader sun are insufficient appearance fixes

Doubling 4096 to 8192 SPP reduces head HDR seed-difference RMS from .434 to
.300, about 31 percent. Display RGB8 RMS rises from 7.72 to 8.83 and the white
pixel counts grow to 906/845. Rare paths enter more pixel estimates before
those estimates become stable. The higher-sample captures remain noisy;
neither is a ground-truth reference.

The private broader-sun preview changes only the shared solar angular radius,
from .004675 to .0187 radians, in both atmosphere and traced emitter. Existing
direct illumination strength is retained and traced radiance uses the existing
area normalization. At 512 SPP its HDR seed difference falls from 1.133 to
.303, but RGB8 difference rises from 4.55 to 9.06 and seed-19 white pixels grow
from 375 to 950. It produces a denser glint pattern and is not an obvious
visual cleanup at this budget. This is a changed light, not improved sampling
of the original light.

The observed mechanism matches the difficult solar/specular paths discussed
in [PBRT](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer#sec:path-regularization).
The current sharp branch follows delta directions, and only the first two
events split Fresnel branches. Ordinary rough-interface light sampling does
not directly connect these longer sharp chains to the sun.

## Proposed bounded next pass

Prioritize visible finish at a useful demo budget:

1. Separate terminal solar radiance after glass paths from smooth sky radiance.
   Keep opaque-base sunlight, shadowing, material detail and shared atmosphere.
2. Compare raw output, a small solar-only outlier filter, and an explicit solar
   specular strength reduction. These have different appearance/bias tradeoffs;
   the raw path remains selectable. Do not filter all glass refraction together.
3. Judge 128/512-SPP whole-scene and head views under independent seeds, then a
   short native orbit/settle. Inspect repeatable highlights and fine grooves for
   lost detail, halos and popping. Adopt a default only after visual review.

This is a proposal, not implemented or adopted. It avoids making another
optical-normal or mesh-repair project a prerequisite for a visible demo trial.
The threshold for proceeding is a clear whole-scene improvement at practical
cost; exact optical conservation is outside this appearance experiment.

[Cycles](https://docs.blender.org/manual/en/4.5/render/cycles/render_settings/light_paths.html#clamping)
documents the brightness tradeoff of clamping. A full reconstruction route
needs a guide/history design: [NRD](https://github.com/NVIDIA-RTX/NRD) describes
its surface guides and special transparency handling. For a separate sampling
study, [specular manifold sampling](https://rgl.epfl.ch/publications/Zeltner2020Specular)
provides dedicated connections through specular chains. Those are larger
follow-ups than the proposed appearance trial.

All captures and probes have finite nonnegative results and clean Vulkan
synchronization validation. High-sample smooth captures retain one/two
cumulative event-budget pixels; there are no medium/query/shadow/stack errors.
Both sampled pixels have zero status flags. Q1's known interface/footprint
limitations remain separate. Ordinary source/build, mesh, recipes, production
budgets and the separate SDF worktree are preserved during this investigation.
