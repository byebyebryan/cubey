# Dragon glass noise-control results

2026-11-16 implementation and appearance trial following the
[established-solutions research](raytracing-dragon-glass-noise-solutions.md).
The [evidence record](../evidence/raytracing-dragon-glass-noise-controls-2026-11/README.md)
contains the matched captures, analysis, validation and browser report.

## Result and recommendation

The user selected **cap 100** as the native demo default after reviewing the
comparison on 2026-11-16. It removes most
isolated white points without the additional softening of color-only OIDN.
For settled stills, **cap 100 plus OIDN** gives the cleanest result in this
comparison. The underlying sharp-glass material and broad refractive patterns
retain their appearance. Fine head bands soften with OIDN, and denoising raw
solar spikes can produce larger pale blobs.

| Head crop, 512 samples | Independent-seed RMS, RGB8 | Reduction from raw |
| --- | ---: | ---: |
| Raw | 4.55 | 0% |
| Cap 100 | 2.46 | 46% |
| OIDN alone | 3.14 | 31% |
| Cap 100 + OIDN | 1.57 | 65% |

At 128 samples the same comparison gives 3.46, 3.01, 3.23 and 1.87 respectively.
Cap 1,000 is less effective; cap 10,000 barely changes displayed speckles.
Cap 100 reduces head mean HDR luminance by roughly 4–5% relative to the matched
noisy raw image. It also suppresses legitimate glints. These are two seed
realizations, rectangular crops including background, and biased treatments;
lower seed difference is not an accuracy measurement. Earlier high-sample
images remain noisy and are not ground truth.

The `dragon-optics` scene and canonical glass preset now default to 100. The
native control starts enabled; `--firefly-clamp 0` or unchecking it restores raw
output. Explicit raw saved views retain zero, and other scenes retain their
defaults. The slider exposes the tradeoff. OIDN remains an
offline still-image study. Native temporal reconstruction, guide generation,
footprint-aware emitter filtering and optical-normal corrections are separate
possible experiments, not required to accept this small demo control.

The comparison report records the earlier opt-in trial. The subsequent
[default-promotion checks](../evidence/raytracing-dragon-glass-clamp-default-2026-11/README.md)
verify cap-100 default images, explicit raw output and saved raw-view reload.

## Renderer contract

`--firefly-clamp` accepts finite values from 0 to 1,000,000. Positive values
require `dragon-optics`; zero retains raw transport. Scene defaults apply only
when the setting is unassigned, so explicit CLI/config zero survives. The uniform is included
in accumulation identity, so a threshold change resets history. Saved views
retain it independently of smooth-normal choice and material recipes.

For a weighted HDR deposit `d`, compute `mean = (d.r + d.g + d.b) / 3`. If the
mean exceeds the cap, multiply all RGB channels by `cap / mean`. Apply it before
adding the deposit to the per-sample result and progressive history. This is
the conventional hue-preserving contribution-cap baseline from the research,
not a display white clip or a change to the emitter. Primary visible atmosphere
is excluded. Emission/diffuse/sheen diagnostics receive the corresponding scale.
Transport sampling, geometry, materials and query budgets are unchanged.

```sh
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/dragon-glass.json --firefly-clamp 100 --samples 512
```

## Offline OIDN baseline

The official [OIDN 2.5.1 binary release](https://github.com/RenderKit/oidn/releases/tag/v2.5.1)
was installed only under the ignored experiment output directory. The archive
SHA256 matches the digest published by GitHub. No global package or renderer
dependency was added. The comparison uses the RT HDR filter, high quality,
CUDA on the RTX 5070 Ti, color only, automatic input scale, and PFM HDR I/O.
There are no albedo/normal guides or temporal history.

The offline ACES/sRGB transform was calibrated against all engine PNGs:
maximum error one RGB8 code value, mean below 0.047. All variants use the same
exposure. Repeated 1080p filter execution is about 14 ms in the isolated CLI;
startup, file I/O, first execution and end-to-end process time are reported
separately. This is not native frame-time evidence.

## Acceptance boundary

The evidence pins the executable, shaders, preset, model and material recipes.
All 16 full-HD renders are finite/nonnegative, synchronization-validation clean
and pass the retained transport policy. Error flags remain zero; zero-weight
termination counts match between caps. Raw-off display parity is retained to
one code value in a single channel/pixel across the four images; HDR float32
arithmetic is not bit-identical. Optical controls, typed config, saved-view
roundtrip and native history reset are checked separately.

The native UI exercise runs a real Vulkan window on an owned Xvfb display. It
checks raw → 100 → raw and saved values, not physical-display latency or
temporal-denoiser quality. The report compares settled stills. No commit, push
or public project-site update is part of this pass.
