# Shared-sky ray-tracing optics results

Date: 2026-11-05. Runtime: `dd432598`, following `2a99e523`.
Branch: `raytracing-first-pass-20261104` in the isolated
`/home/bryan/code/cubey-raytracing-first-pass` worktree.

## Result

The [optics plan](raytracing-optics-plan.md) now has a native implementation:
shared clear-sky illumination, sharp floor/curved-mirror scene reflections,
and a closed clear-glass prism that refracts neighboring geometry. The dragon
remains opaque. Diffuse GI is unnecessary for this bounded optics demonstration;
it remains a separate lighting milestone.

The renderer reuses `AtmosphereEnvironmentRuntime`, shared sun/diffuse SH,
prefiltered sky/DFG, the GPU owner, window/headless hosts, capture and HDR post.
The only shared runtime extension declares probe consumer stages, so both sides
of texture reuse synchronize RT readers while existing fragment defaults stay
unchanged. No new graph domain, GPU queue, cloud field or parallel sky owner was
introduced. The radian RT controls retain their +Z azimuth convention through
an explicit conversion to the atmosphere's degree/−Z convention.

The finite scene floor occludes real rays. The distant sky cache uses the
existing `SkyOnlyNoGroundOcclusion` policy; its lower hemisphere is an environment
approximation. It is not per-segment atmospheric integration. Appearance edits
reuse AS resources; resize replaces only target resources.

## Transport and native evidence

Sequential trace calls in ray generation implement a deterministic Whitted
split, with pipeline recursion depth one. Exact unpolarized dielectric Fresnel,
Snell bending, TIR, Beer absorption and radiance eta-squared weights are used at
geometric glass boundaries. Interpolated normals shade opaque and curved mirror
surfaces, with a geometric-side check. Mirror/glass traced lobes do not also add
cached IBL. Opaque rough specular remains an environment approximation.

The default budget is 8 events / 32 radiance calls / 128 total calls, with
throughput cutoff 1e-4 and at most 8 straight shadow interfaces. The control is
12 / 64 / 256 with no cutoff. Fixed-size pending paths, rejected medium states
and discarded throughput are observable. An exhausted path does not acquire
fallback sky radiance. Glass sun shadows approximate straight transmittance;
they do not trace bent sun paths or caustics.

Three frozen presets provide reflection overview, curved mirror and prism
detail. Paired environment-only/scene images keep all other settings fixed.
The green marker is behind the primary camera and appears in the curved mirror.
The 120-frame native camera sequence is 1280×720, 30 fps, 4 seconds; AS builds
remain fixed. Low-sun and sunset stills use the same lighting adapter and exposure.
The [project guide](../../projects/raytracing/README.md) gives exact commands.

## Numerical qualification

Independent double-precision CPU rays/interface math score native GPU
observations. Tolerances were fixed before qualification and were not relaxed.
First-pass geometry retains depth 2e-5, barycentrics 2e-5 and normals 0.1 degrees.
The four cameras contain 49,152 rays, with zero scored shadow mismatches.

| Optical quantity | Fixed limit | Observed maximum |
| --- | ---: | ---: |
| Unit direction | 1e-05 | 3.8547943e-06 |
| Secondary position, metres | 0.0001 | 5.0268561e-05 |
| Absolute Fresnel | 1e-05 | 8.4155424e-06 |
| Relative absorption | 0.0001 | 1.1459481e-07 |

Four IOR values (1, 1.33, 1.5, 2.5) each exercise 4,096 native interface/thickness
inputs. The fixed critical/grazing band excludes 32 IOR=1 inputs; all other
16,352 inputs are scored. Reflection and transmission classification are checked
outside that band. The oracle uses the float32 IOR actually admitted by the
native configuration, avoiding a decimal-vs-float precision discrepancy at
grazing incidence. The initial failed receipt is retained in ignored outputs.

Generated scene checks include an equivalent virtual camera for the floor,
curved reflections, an off-screen target, slab/prism entry/exit and neighboring
opaque hits, transparent shadows, IOR=1 and lossless environment transport.
The virtual-camera oracle includes the production normal offset. Strict
secondary comparisons exclude declared triangle-edge guard bands. The off-screen
case has zero primary green pixels and six sphere-reflected green pixels.
The transparent-shadow case scores 83 partial transmissions. Lossless environment
transport has maximum absolute RGB error 1.67e-8 across 3,072 pixels.

Five independently aimed primary sky views agree with environment-only mirror
samples within the fixed 0.1% normalized tolerance. A unit grid also proves the
legacy and shared sun-direction conventions match after conversion.

## Full-image convergence

Each row compares all 2,073,600 linear-HDR pixels against the deeper control.
There are no edge exclusions in this comparison. Normalization is
`max(1, control luminance)`; the p99 statistic uses each pixel's largest channel
error. Limits are mean ≤0.1% and p99 ≤1%.

| Frozen view | Mean normalized error | Pixel p99 | Maximum error | Default event-limit pixels |
| --- | ---: | ---: | ---: | ---: |
| reflection | 0.011287% | 0.000000% | 0.687168 | 1,104 |
| curved-mirror | 0.038561% | 0.000000% | 0.656226 | 4,408 |
| refraction | 0.000447% | 0.014222% | 0.000211 | 0 |

All three aggregate limits pass. They do not imply a uniform error bound:
repeated mirror chains have localized large errors, and even the control retains
192/1,157 event-limited pixels in the two mirror views. The prism default flags
seven medium-state rejections (including combined flags) among 2,073,600 pixels;
the exact path-level cause is unclassified. Those flags remain visible and are
not described as zero numerical failures. The deeper control also rejects a
small number of paths. The status/cutoff counts and bounded diagnostic discarded
throughput accompany the evidence. Separate 480×270 diagnostic views record
maximum discarded RGB throughput up to 0.659 in repeated mirrors and 0.421 in
the prism; throughput is not an error in final radiance. Their largest pending
stack is three and largest total trace count is 21. Deliberately reducing total,
event and radiance budgets separately proves terminated mirror paths retain
positive discarded throughput and zero acquired fallback radiance. Human artistic acceptance remains separate.

## Lifecycle and regression qualification

The owned Xvfb optical check exercises orbit/zoom, reset, light/material/IOR edits,
reflection/transmission route changes, diagnostic views, save and two resize
transitions. It retains seven AS builds and three target generations. The legacy
window route retains three AS builds and three target generations. Saved-view
reloads agree outside the UI within one RGB8 level. This is automated native
window evidence; it does not establish physical desktop interaction.

A deterministic moving-sun test observes seven sky generations over 90 frames,
seven AS builds, one target generation and 90 completed total-frame timestamps.
All ten focused RT tests pass with synchronization validation enabled, including
ordinary-device rejection of RT probe stages and partial construction/retry.
Camera-inside-glass input fails with the explicit unsupported-state message and
clean teardown. All validated optical captures are clean of VUID/sync errors.

Legacy RT fixture/triangle/dragon captures are byte-identical to the retained
first-pass PNGs. Representative shared-atmosphere SDF clay, mesh-PBR clear-sky
clay and Atmosphere captures are also pixel-identical to the original build.

The normal `ctest --preset dev --output-on-failure` suite passes all 279 selected
groups.

A deliberately broader `VK_LAYER_VALIDATE_SYNC=1` dev run passes 275/279 tests.
The four failures are pre-existing reused-depth attachment write hazards in
`headless_cube_render_only`, `headless_cube_writes_video`,
`gltf_viewer_headless_profiles_scene` and `pbr_furnace_headless_writes_video`.
All four reproduce with the untouched original `build/release` binaries. These
are retained shared-renderer issues outside this optical change; the clean
focused RT sync result is not presented as a clean global sync suite.

## Costs

The optimized build is RelWithDebInfo (`-O2 -g -DNDEBUG`, Clang 23.1.4), on an
RTX 5070 Ti, driver 610.57.04. Each of six frozen configurations has three serial
180-frame processes, with 30 warmup frames and 150 completed samples per pass.
Validation and raw diagnostic output are disabled; necessary transport budget
counters remain in the implementation. No other owned GPU job, capture or encode
runs concurrently. Source, binary, shaders, presets and model hashes are retained;
quality/timing binaries and all those inputs match exactly. GPU boundary readings
do not prove continuous exclusivity on this shared host.

Ranges below span the three repetition statistics, in milliseconds.

| View / optical route | Trace p50 | Trace p95 | Total-frame p50 | Total-frame p95 |
| --- | ---: | ---: | ---: | ---: |
| reflection / environment | 0.656–0.687 | 0.659–0.691 | 0.672–0.703 | 0.675–0.707 |
| reflection / scene | 0.743–0.752 | 0.746–0.756 | 0.760–0.768 | 0.763–0.772 |
| curved-mirror / environment | 0.656–0.663 | 0.659–0.670 | 0.671–0.678 | 0.674–0.687 |
| curved-mirror / scene | 0.777–0.779 | 0.781–1.015 | 0.793–0.795 | 0.797–1.034 |
| refraction / environment | 0.799–0.810 | 0.808–0.982 | 0.816–0.827 | 0.824–1.001 |
| refraction / scene | 0.934–0.946 | 0.950–1.151 | 0.952–0.963 | 0.968–1.181 |

All frozen and moving-sun total-frame p95 values pass the 16.7 ms screen.
Moving-sun reflection overview has 13 sky generations per process and total-frame
p95 1.616–1.656 ms; the environment-update span p95 is 0.841–0.843 ms. Settled
update spans contain only timestamp overhead. The original `whole_scene` scope
still encloses trace and HDR post; the new `total_frame` also encloses environment
updates. These project GPU spans exclude CPU startup, capture/encoding,
presentation latency and physical interaction. Small outliers across repetitions
are retained; no timing row was dropped or averaged into a single best result.


Startup and allocation scopes remain distinct from completed steady GPU spans.
The representative validated reflection still records 27.97 ms CPU scene
preparation, 24.71 ms scene upload/build, 2.30 ms AS GPU work, 209.28 ms shared
environment setup (199.62 ms generated fallback setup), 0.87 ms initial probe GPU
work and 12.89 ms RT pipeline setup. Launch to completed PNG is about 0.96 s in
that capture. These are single-process observations with different overlapping
scopes and must not be added together or treated as steady-frame costs.

That 1080p headless scene has 887,580 triangles, seven AS builds, 24,915,568 bytes
geometry, 54,915,712 bytes AS, 384 bytes materials, 144 bytes camera uniform and
240 bytes environment uniform. Requested resident/peak project memory is
96,421,232 bytes; separately measured shared environment image allocation
requirements are 3,220,992 bytes. The observed current-process device-local
allocation estimate at shutdown is 144,113,664 bytes. Project requests exclude
host targets, upload staging, padding, driver/pipeline/descriptor internals and
shared probe internals; the environment image figure excludes its uniforms and
is not a whole-process peak. Slot count changes per-slot material/uniform costs.

## Report delivery

[Native optics comparison report](http://starship:8787/reports/8739c7743495181c8fa17a3767c79171cbc7462a9970716b0b3be38d3f4d0a79/). The existing shared report server
was reused and left running. All 43 published bundle files return HTTP 200 and
match the authored SHA-256 bytes; JavaScript syntax, asset links and 54 DOM
interaction states pass. The configured browser runtime failed before navigation
in this session, so browser rendering remains an open acceptance boundary.

## Retention and next boundary

The [evidence index](../evidence/raytracing-optics-2026-11/README.md) contains
compact numerical, identity, native and cost receipts. Native PNGs/video, full
raw diagnostics, logs and the pinned donor build remain under ignored
`outputs/raytracing/optics/` and the existing donor cache. The prepared dragon
identity is unchanged. No GLB or downloaded donor tree is committed or bundled
into the report. The original SDF/horse checkout and first-pass evidence are
preserved; source push, merge and public demo-site publication were not performed.

This pass does not implement diffuse GI, rough scene reflection/refraction,
cloud composition, nested media, camera-inside-glass, caustics, dispersion,
textures, animation, temporal accumulation or denoising. Primary rays are single
center samples. The next decision is whether rough/stochastic transport or
diffuse GI better serves the next native result; neither is implicitly selected.
The matched mesh/SDF comparison still needs its own representation and traversal
contract using the shared scene/light/material vocabulary.
