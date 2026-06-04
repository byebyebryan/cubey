# Ocean Performance Notes

Date: 2026-06-03

These notes capture the current spectral-ocean cost model for
`projects/ocean`. They are working notes, not final architecture guidance.

## Current Defaults

The active ocean renderer defaults to:

- `512 x 512` FFT maps;
- half-precision `RGBA16F` wave-field textures;
- two enabled cascade slots, C0 and C1;
- packed FFT storage fields, where four logical complex outputs are stored in
  two RGBA images;
- lazy allocation for enabled cascades only, with inactive surface descriptors
  bound to a tiny fallback field;
- per-cascade map-size overrides and per-cascade update intervals exposed in
  the UI.

`1024` remains useful as a maximum-quality inspection mode. It is not the
required default target. `256` remains useful for smoke tests and very fast
checks, but currently loses too much wave, normal, and foam detail for the main
presentation path.

The current user-visible readout on the NVIDIA RTX A2000 8GB Laptop GPU is
that the renderer is compute-bound: changing debug views does not materially
change frame rate because the wave compute path still runs before drawing.

## Why FFT Is Used

The ocean surface is a spectral ocean. Each cascade starts from a regular 2D
frequency grid. Conceptually, a `1024 x 1024` map contains roughly one million
Fourier basis waves, although not every coefficient has meaningful energy and
positive/negative frequency pairs are related.

For one spatial sample, the surface could be evaluated by directly summing all
the sine/cosine components:

```text
height(x, z) = sum over k: h(k, t) * exp(i * dot(k, position))
```

That is practical for a small set of explicit Gerstner or Stokes waves. It is
not practical for a dense spectral grid, because direct evaluation would
multiply the number of output texels by the number of frequency samples. A
`1024 x 1024` grid would imply roughly a trillion component evaluations per
logical field.

The inverse FFT computes that same dense regular sum for the full tile in
roughly `N^2 log2(N)` work. FFT is therefore not required for waves in general;
it is the efficient transform for dense spectral waves.

## Current Compute Pipeline

For each enabled cascade, the active renderer uses this GPU path:

1. `ocean_spectrum.comp`: generate the initial `h0(k)` spectrum. This runs only
   when the spectrum is invalidated, such as first use, map-size or precision
   changes, cascade allocation changes, or wave-source config changes.
2. `ocean_modulate.comp`: apply time evolution in frequency space and write
   two packed complex FFT fields.
3. `ocean_fft.comp`: run a staged 2D inverse FFT over each packed field.
4. `ocean_unpack.comp`: split the packed transformed fields into sampled
   displacement, normal, and foam textures.
5. `ocean.vert` / `ocean.frag`: sample those textures for mesh displacement,
   shading, foam, diagnostics, and wave self-shadowing.

The steady-state frame cost is dominated by the FFT stage, not by spectrum
initialization.

## Cost Model

The default uses two enabled cascade slots, C0 and C1. The renderer has five
slots available, but disabled slots skip spectrum, modulate, FFT, and unpack
dispatches. Disabled slots also skip full wave-resource allocation.

For an enabled cascade at map size `N`, steady-state compute is:

```text
1 modulate dispatch
+ 2 packed fields * 2 axes * log2(N) FFT dispatches
+ 1 unpack dispatch
```

At the current default `512`, the two-cascade steady-state cost is:

```text
2 modulate dispatches
+ 2 cascades * 2 packed fields * 2 axes * log2(512) FFT dispatches
+ 2 unpack dispatches
= 76 full-map compute dispatches per frame
```

The FFT portion alone is:

```text
2 * 2 * 2 * 9 = 72 dispatches per frame
```

For comparison, the same packed-field path at `1024` would be 84 total
dispatches per frame, with 80 of those in FFT. The original unoptimized shape
used four FFT fields and would have required 164 total dispatches for two
cascades at `1024`.

Resolution still scales approximately as `N^2 log2(N)`, so map size dominates
cost even after dispatch count and memory format improvements:

| Map size | Approx. FFT work vs 1024 |
| --- | ---: |
| 1024 | 100% |
| 512 | 22.5% |
| 256 | 5.0% |

Per-cascade update intervals can skip recomputation on selected frames. They
are useful for far or candidate cascades, but the default C0/C1 core still
updates every frame.

## Memory Shape

Each enabled cascade currently allocates these map-sized wave textures:

- `h0`: 1
- packed modulated fields: 2
- FFT ping buffers: 2
- FFT pong buffers: 2
- final displacement, normal, and foam: 3

That is 10 map-sized textures per enabled cascade. At the default `512` with
`RGBA16F`, each texture is roughly 2 MiB, so each active cascade is roughly
20 MiB of wave-field storage. The default C0/C1 allocation is therefore roughly
40 MiB plus small fallback and descriptor overhead.

Useful comparison points:

| Mode | Per texture | Textures/cascade | Approx. storage/cascade |
| --- | ---: | ---: | ---: |
| 512 `RGBA16F` | 2 MiB | 10 | 20 MiB |
| 512 `RGBA32F` | 4 MiB | 10 | 40 MiB |
| 1024 `RGBA16F` | 8 MiB | 10 | 80 MiB |
| 1024 `RGBA32F` | 16 MiB | 10 | 160 MiB |

The old all-slot, 16-texture, `1024 RGBA32F` shape was roughly 1.25 GiB. The
current default is much smaller because inactive cascades are not allocated,
the FFT fields are packed, and half precision is the default.

## Design Readout

The implementation still ties several concerns to FFT map size:

- spectral richness;
- spatial displacement texture resolution;
- normal and foam resolution;
- Jacobian/compression diagnostics;
- near-field material detail.

The current direction is a hybrid model:

- keep FFT for coherent broad/mid ocean motion;
- keep `512` and half precision as the practical default while visual quality
  remains acceptable;
- keep `1024` and full precision available as comparison modes;
- add cheaper close-up detail through procedural/detail normal and foam paths;
- consider explicit Gerstner/Stokes or other analytic macro features for
  sharper art-directed crests;
- separate displacement, normal/detail, and foam update needs instead of
  forcing every concern through the same resolution and update rate;
- use lower update rates or lower resolutions for far/candidate cascades;
- only attempt a deeper FFT rewrite after pass-level profiling confirms the
  specific bottleneck.

## Optimization Guardrails

Before changing the algorithm again, capture measurements that include:

- per-pass GPU timings for spectrum, modulate, FFT, unpack, draw, self-shadow,
  atmosphere, and post;
- active cascade count, per-cascade map sizes, per-cascade update intervals,
  field precision, and enabled feature flags;
- memory usage for all ocean wave resources;
- comparisons at `1024`, `512`, and `256`;
- comparisons between `half` and `full` precision;
- comparisons with wave self-shadow, foam history, and optional cascades
  disabled.

Do not treat `1024` as a required quality target. Treat it as a current
maximum-quality brute-force mode. The performance goal is to preserve or recover
the visible quality of `1024` with a cheaper distribution of work.
