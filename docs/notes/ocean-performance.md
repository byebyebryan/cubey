# Ocean Performance Notes

Date: 2026-06-03
Updated: 2026-07-23

These notes capture the current spectral-ocean cost model for
`projects/ocean`. They are working notes, not final architecture guidance.

## Measured Baseline

The first whole-ocean GPU baseline was captured on 2026-07-23 at commit
`521da2d503d69b3778480658b86f4cdf68bdd5a6` on the NVIDIA GeForce RTX 5070 Ti.
Every lane used `1600 x 900`, Windy, 180 frames, 30 warmup frames, and 147
steady GPU samples. The harness rejected concurrent external compute work.

`total` is the per-frame sum of every recorded ocean GPU span. `wave` sums
modulate, FFT, and unpack for both active cascades; one-time spectrum
initialization is outside the warmed steady-state window.

| Lane | Total mean / p50 / p95 | Wave p50 | Scene p50 | Cloud p50 |
| --- | ---: | ---: | ---: | ---: |
| 256 half, mid, clear | 1.855 / 1.855 / 1.865 ms | 0.153 ms | 1.689 ms | 0.000 ms |
| 512 half, mid, clear | 2.263 / 2.262 / 2.271 ms | 0.318 ms | 1.930 ms | 0.000 ms |
| 1024 half, mid, clear | 3.351 / 3.350 / 3.360 ms | 1.041 ms | 2.294 ms | 0.000 ms |
| 512 full, mid, clear | 3.358 / 3.356 / 3.364 ms | 0.427 ms | 2.915 ms | 0.000 ms |
| 512 half, low, clear | 1.643 / 1.643 / 1.656 ms | 0.318 ms | 1.311 ms | 0.000 ms |
| 512 half, high, clear | 3.266 / 3.268 / 3.281 ms | 0.318 ms | 2.937 ms | 0.000 ms |
| 512 half, mid, clouds cached | 3.954 / 3.878 / 4.990 ms | 0.320 ms | 1.936 ms | 1.610 ms |
| 512 half, mid, clouds planar | 4.408 / 4.332 / 5.442 ms | 0.317 ms | 1.953 ms | 2.050 ms |

The default clear path is primarily surface rendering, not FFT compute:
`ocean scene` accounts for about 1.93 ms of the 2.26 ms p50 while steady wave
generation accounts for about 0.32 ms. Camera altitude changes scene cost from
1.31 ms at `low` to 2.94 ms at `high` while wave work stays constant. This
points first to submitted clipmap and fragment/material work, then to FFT
optimization.

The full-precision lane adds about 1.09 ms over half precision, but most of the
observed delta is in `ocean scene`, not the wave-compute spans. Treat this as a
field-sampling/bandwidth lead to investigate rather than an established cause.
The 1024 lane increases both wave and scene cost as expected.

Cloud composition adds about 1.62 ms p50 with the cached reflection source.
The planar reflection product adds another 0.45 ms. Both cloud lanes have an
approximately 1.1 ms periodic cloud-environment refresh, which explains the
higher p95 without affecting the steady p50.

Observed device-local allocations were about 98 MiB for 256 half, 128 MiB for
512 half, 248 MiB for 1024 half, 168 MiB for 512 full, and 440 MiB for the
composed cloud lanes. These are whole-process allocations, not wave-texture-only
estimates.

Reproduce the matrix with:

```sh
projects/ocean/profile_ocean_baseline.sh outputs/ocean/performance-baseline
```

Raw captures and profiler artifacts remain ignored under `outputs/`. Headless
profiles now report configured/effective mesh cells, horizon planning, generated
patches/triangles, and submitted patches/triangles.

The follow-up [scene ocean performance study](ocean-scene-performance-study.md)
keeps the original scene pass intact and attributes surface cost through matched
background-only runs. It records the close/low/mid budget verdict, controlled
mesh and material ablations, cloud-product cost, and the ranked optimization
order.

## Current Defaults

The active ocean renderer defaults to:

- `512 x 512` FFT maps;
- half-precision `RGBA16F` wave-field textures;
- two enabled cascade slots, C0 and C1;
- packed FFT storage fields, where four logical complex outputs are stored in
  two RGBA images;
- lazy allocation for enabled cascades only, with inactive surface descriptors
  bound to a tiny fallback field;
- footprint-adaptive surface shading, preserving resolved patches while using
  static bilinear filtering and four self-shadow steps beyond the far-detail
  footprint boundary;
- per-cascade map-size overrides and per-cascade update intervals exposed in
  the UI.

`1024` remains useful as a maximum-quality inspection mode. It is not the
required default target. `256` remains useful for smoke tests and very fast
checks, but currently loses too much wave, normal, and foam detail for the main
presentation path.

The current RTX 5070 Ti scene study supersedes the older A2000 whole-renderer
readout for optimization priority. At the default 512-half source, fixed wave
generation is about `0.318 ms`; camera-dependent surface rendering is the
larger cost and now uses footprint-adaptive shading.

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

Before changing the algorithm again, keep measurements that include:

- non-overlapping GPU timings for spectrum, modulate, FFT, unpack, the combined
  scene, cloud products, and post;
- matched background-only and feature-ablation runs for surface and
  self-shadow attribution; do not split the production scene pass or insert
  serializing nested timestamps;
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

## Deferred Optimization Backlog

The recent packed-field, lazy-allocation, and half-precision changes were mostly
behavior-neutral implementation optimizations. Further high-gain work should be
classified before implementation so we do not mix invisible optimizations with
quality/performance policy changes.

Behavior-neutral candidates:

- improve the FFT implementation while preserving the same spectral inputs,
  outputs, and update cadence;
- reduce FFT dispatch/barrier overhead or improve memory locality;
- overlap independent GPU work without skipping recomputation;
- remove redundant shader math in modulate, unpack, self-shadow, or material
  stages;
- clean up descriptor, pipeline, and resource churn if profiling shows CPU-side
  cost.

Quality/performance policy candidates:

- use mixed per-cascade resolutions such as C0 at `512` and secondary cascades
  at `256`;
- update far or secondary cascades every 2+ frames;
- add procedural/detail normal and foam layers to recover close-up detail
  outside the FFT path;
- define named quality presets that set map sizes, cascade masks, precision,
  update intervals, and expensive feature toggles.

Keep the behavior-neutral path first if the goal is to preserve the current
look. Treat policy changes as presentation tuning and validate them with side by
side captures against the current default.
