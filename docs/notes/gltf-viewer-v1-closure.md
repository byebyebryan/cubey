# glTF Viewer V1 Closure

Status: accepted and closed on 2026-09-12 at Cubey revision
`97bac589f351b5d74140f086587db639b0a3155c`.

## Decision

The first single-asset glTF Viewer milestone is complete. It is no longer a
default active workstream: further compatibility, throughput, streaming, or
optical work needs a concrete asset/product failure or a measured workload.

This decision combines three bodies of evidence rather than treating a single
smoke capture as conformance:

- staged loading: metadata-only bounds probing, worker-side load/preparation,
  bounded GPU-owner residency, complete-generation activation, safe retirement,
  and measured windowed upload responsiveness;
- material semantics: focused loader/shader contracts, deterministic furnace
  fixtures where suitable, and pinned Khronos end-to-end compatibility and
  semantic captures;
- dynamic optics: controlled motion and static-repeat review for ordering,
  rough refraction, solid-volume exit, attenuation, dispersion, and the primary
  procedural-environment path.

## Validation Snapshot

The clean closure revision passed:

- `ctest --preset dev`: 116/116 tests;
- `ctest --preset dev-gltf-conformance`: 60/60 tests against clean Khronos
  Sample Assets revision `2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf`;
- the dynamic-audit self-validator: six H.264/yuv420p, video-only 1280x720
  60 FPS clips; exact frame counts and durations; asset, binary, environment,
  clip, still, and analysis hashes; decoded static-repeat equality; and no
  surviving capture processes.

Those counts describe the pinned closure snapshot, not an evergreen promise
about future test inventory.

## Dynamic Optics Matrix

Moving lanes use a six-second, 360-frame smoothstep orbit from -15 to +15
degrees. Static HDR lanes use Filament's `lightroom_14b.hdr` at exposure 0 and
IBL intensity 1. The procedural lane fixes solar time at 14:00, pauses time and
animation, and disables clouds. All lanes use ACES and a 0.45 scene-bounds
camera-distance scale.

| Lane | Scene and purpose | Capture | Result | Source-video SHA-256 |
| --- | --- | --- | --- | --- |
| A | `TransmissionOrderTest`: alpha/transmission ordering | static HDR orbit | pass | `5a90687e9231d706b92af9946b62b6942e53dc8258b5f7250ffdf4c6d7c46e09` |
| B | `TransmissionRoughnessTest`: rough-refraction mip stability | static HDR orbit | pass | `a8c5d25080a8a671ada477e1bee5bd764d465bf1bb5d396b40159367f7ebdb75` |
| C | `DragonDispersion`: exit, attenuation, and dispersion | static HDR orbit | pass | `52b8307b8fb2c43de8d36e5d67b5d0f97fded7c7ccd7f6f9bb4c0d5b3f402a46` |
| D1/D2 | `DragonDispersion`: independent fixed-view repeats | two 120-frame static clips | pass; decoded frames identical | `609cfeee6d639f09c2c36c8c5d4be6b9037a17288d71ee72a4d2ad60e5a727a9` (each) |
| E | `DragonDispersion`: procedural-environment coherence | fixed-time atmosphere orbit | pass | `bf4a42e0c5b0edf6ef75d7b274c109a3106dd7a38ceea5dca1b2df473d6c36dd` |

Adjacent-frame luma differences were used only for triage. The flags across
A/B/C aligned with H.264 I-frame boundaries at the two- or four-second GOP
transitions. E produced one marginal non-GOP flag near the midpoint. Native
lossless stills at exact smoothstep yaws `-0.188017907`, `-0.062673933`, and
`+0.062673933` degrees were visually continuous, so no renderer discontinuity
survived review.

The full local pack—including exact argument arrays, input and output hashes,
framing probes, source clips, keyframes, contact sheets and outlier triage,
adjacent-frame CSV/JSON data, lossless recaptures, scripts, and validator—lives
under ignored `outputs/gltf-optics-dynamic-audit-20260912/`. It was intentionally
not committed as product media. The referenced sample assets and HDR retain
their upstream license boundaries.

Audit provenance:

- Cubey binary SHA-256:
  `adfdefe827effea643701a15dee13b0d7ea16d40cc83476eed0c6063bed7a7a6`;
- static HDR SHA-256:
  `adccd934033f8a6f71ee25208c5cc082ee5f8989c4ad66ea001d795760952b15`;
- capture device: NVIDIA GeForce RTX 5070 Ti, driver 615.71.09.

## Accepted Limits And Reopen Triggers

The following are explicit V1 boundaries, not hidden conformance claims:

- refraction is screen-space and fades to the selected environment near/off the
  screen edge;
- thick volume uses a sphere-style approximate second interface rather than
  exact mesh back-face exit reconstruction or multiple internal bounces;
- the bounded alpha/refraction-source strategy retains a stable front-object
  echo instead of providing exact front/behind classification or OIT;
- direct transmitted light covers Cubey's selected directional light, not
  generalized `KHR_lights_punctual`, spectral direct-light dispersion, colored
  transparent shadows, or point/spot arrays;
- the current sheen IBL reuses the GGX-prefiltered environment, anisotropy uses
  a bent-normal approximation, and fallback tangents are not MikkTSpace.

Reopen glTF work when one of those boundaries produces a visible failure in a
real target asset, when a required format feature blocks a product, or when a
measured multi-asset workload justifies loader concurrency or queue changes.
Do not reopen it merely to chase a larger extension checklist or replace a
working approximation without demonstrated product gain.

Related current documents:

- [glTF assets and PBR](../architecture/gltf-assets.md)
- [PBR and IBL](../architecture/pbr-ibl.md)
- [glTF staged-loading profile](gltf-loading-profile.md)
- [glTF Viewer project guide](../../projects/gltf_viewer/README.md)
