# Fluid 2.5D terrain material study V3

Evidence: `outputs/fluid/terrain-material-v3-20261007-8vS8oV/index.html` and
`RESULTS.md`, with runtime/source/input/media hash verification.

Status: opt-in study; human visual acceptance deferred. Readable remains the
Mountain Rain launcher default; Scenic still defaults to refined V2. No solver,
worker, bed/scale, field cadence, film/coverage, bank geometry, dots, generated
detail or water shader change within this study. No new texture assets. The
original loop left changes uncommitted for review; the later commit review
retains the opt-in study and its historical evidence.

## Findings

The conspicuous bright cliff bands are strongest in terrain ambient lighting
with water hidden, including the no-detail-normal control. Generated irradiance
retains a narrow key from `generated_radiance`, rather than a diffuse cosine
convolution. Terrain-only five-direction softening reduces that appearance but
is an artistic approximation; water's existing environment remains unchanged.

Normal-only changes are subtle at retained kilometre-scale cameras. Neutral
weathered soil/rock colors and dry roughness improve water/ground separation.
Close views still expose broad 60m-support normals and grid-scale ground
shadow/film boundaries; no silhouette or stepped-bank fix is claimed.

The new GLSL surface-gradient subset has a pinned MIT donor and full notice
under `third_party/surface_gradient/README.md`. It uses ZY/XZ/XY projections,
separates classification/detail normals, reconstructs generated RG normals
without a green flip, and fades distant normal detail. Its 80 analytic Vulkan
controls cover both slope signs, seams, flat/zero/distant identity, normalized
outputs and bounded extreme derivatives.

## Controls

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_mountain_rain_demo.py replay --style scenic --material terrain
```

The Scenic material panel exposes V1, refined and terrain presets; neutral
material blend, surface-gradient normal toggle/strength, ambient softening,
and twelve terrain/component views. `--fluid25d-scenic-terrain-view` offers the
same diagnostics. Non-shaded terrain views suppress water/dot draws, never
uploads. Colors retain Scenic exposure/tonemapping, so these are presentation
diagnostics rather than calibrated vector maps.

New bounded JSON values under the retained v2 schema:

- `terrain_material_blend`: 0..1; legacy profiles 0, terrain 1.
- `terrain_normal_strength`: -1 (legacy shortcut) or 0..2; terrain 0.12.
- `terrain_ambient_softening`: 0..1; legacy profiles 0, terrain 1.

Native camera heading is independently adjustable. A headless recorded video
can use `--fluid25d-native-camera-sweep` for a bounded heading sweep/mild zoom;
camera receipts are emitted without changing requested/saved native fields.

## Acceptance boundary

169 dev tests pass plus focused post-orchestration-fix helper checks; calm/gap/
one-cell/lake/bank compatibility and private Xvfb style/resize checks pass.
Eleven legacy V2 images and seven Readable/raw controls are byte-identical.
Forty existing compiled shaders are unchanged; the legacy terrain fragment is
retained as a separately byte-identical shader. Native uploads remain bit-exact
with zero replay hydraulic dispatches.

Three interleaved 12-warmup/108-measured-frame batches give median increases
of 0.017ms at 720p and 0.051ms at 1080p. The 4ms 720p p95 ceiling passes, but
individual paired tails exceeded the proposed incremental targets. No stable
tail-latency/FPS improvement or live shared-CUDA-GPU acceptance is claimed.

The review contains two eight-second comparisons, four paired stills, component
diagnostics, alternate/close views, moving-camera and held-state checks. Failed
orchestration attempts and exact prior tool snapshots are retained; renderer
source is still current-bound. See RESULTS for measured uncertainty and
verification command in the project review runner.

Recommendation: retain these trade-offs as options. A later bounded rendering
study should evaluate proper Scenic-only diffuse irradiance and ground shadow
readability before adding close-up material assets; avoid expanding into
terrain generation, shoreline reconstruction or new hydraulic work.
