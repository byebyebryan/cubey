# SDF environment, lighting and reflection presentation pass

Date: 2026-10-23

Status: implementation loop authorized on 2026-10-23. The user accepts the
latest material contrast pass. The following sections retain the reviewed
boundary and checkpoints; implementation results will be recorded separately.

## Objective and retained baseline

Give the accepted sphere/guardian materials a more interesting environment:
coherent visible sky, direct light, diffuse sky response, reflected sky/clouds
and glass environment refraction. Retain the existing fixed environment as a
material-comparison baseline. Keep guardian geometry, material records, stone
patterns, rough diffuse behavior and field resolution unchanged.

Current implementation checkpoint: `33215df4`; renderer/material implementation
is in `1812e7c8`. The accepted [material contrast report](http://starship:8787/reports/d7311c3fe7081ab555c02dad8742497d9cc57fb04424c652a350adf95641eb51/index.html)
is the before reference. This pass changes presentation and a bounded glass
quality option; it does not reopen modeling or material look development.

## What exists and what is missing

- `projects/sdf/sdf_renderer.cpp` owns an `AtmosphereEnvironmentRuntime`, but
  currently uses its lighting/frame values without creating its reflection
  resources or cloud products. The legacy `atmosphere` route evaluates the
  shared clear sky and uses its direct/diffuse lighting.
- `material-study` uses the shared **generated test environment**, with fixed
  key/fill lobes and a sky/ground gradient. Its direct key is fixed too. This
  route provides the accepted metal/glass/rough-diffuse shading, but does not
  consume atmosphere or cloud reflection products. Changing its sun fields
  cannot currently produce the proposed dawn/dusk presentation.
- The shared atmosphere runtime already owns coherent clear-sky reflection
  capture/prefiltering and a `CloudEnvironmentRuntime`. The glTF consumer in
  `projects/gltf_viewer/gltf_viewer_assets.cpp` and `gltf_viewer_render.cpp`
  demonstrates resource creation, updates and visible cloud composition.
  Reuse the shared runtime; do not copy the cloud marcher into SDF or reopen
  the retired/reference cloud studies.
- `AtmosphereEnvironmentRuntime::pbr_environment_bindings` starts with fallback
  bindings and replaces the **specular** probe/transition state. It does not
  replace the generated diffuse cube. Merely changing SDF's reflection texture
  would therefore leave an unrelated diffuse environment in the new route.

## Glass budget: compare 4, 8 and 16

Increasing crossings is a sensible small experiment. It allows more sequential
air/glass entries and exits through separated/concave pieces. It does not turn
the path into recursive optical transport or improve the current TIR fallback.

Proposed render option: `--glass-crossings 4|8|16`.

- Retain **4 as the compatibility default** so saved old presets and captures
  preserve their meaning. Use **8 as the candidate for new presentation
  presets**, subject to the comparison below. Keep 16 as an explicit quality
  option if it earns its cost. Do not silently rewrite the accepted old scenes.
- Count interfaces including the primary hit. Keep the same geometric normals,
  Snell/Fresnel calculation, absorption distance, trace tolerances and hard
  traversal/progress failure signals. Use a bounded shader loop of at most 16.
- This is a render-quality setting in presets/uniforms, not a material or
  geometry property. Changing it must not rebuild distance bricks or change
  authored scene hashes/exports. Validate supported values and preserve it in
  config write/reload. A native 4/8/16 selector is sufficient.
- Keep total internal reflection's current environment fallback. Keep its
  diagnostic separate from crossing-cap fallback and traversal errors. More
  crossings cannot resolve rays that already terminated at TIR.
- Retain the **1% worst-view crossing-cap gate** and zero error/exhaustion gate.
  The old guardian's 1.1068% at four crossings remains a historical result;
  increasing the quality budget requalifies the new setting rather than
  rewriting that result. TIR fallback currently covers roughly 39–60% of the
  sampled guardian glass pixels and remains a substantial approximation.

First compare 4/8/16 under the existing fixed environment. Use the sphere,
separated-solid and cavity fixtures, guardian hero/detail views and 24 evenly
spaced guardian orbit angles including the earlier eight-angle sample set.
Record cap/TIR/error masks and fractions, and identical-setting sphere images.
The sphere should exit before the additional budget is needed. With unchanged
TIR behavior, increasing the cap should not increase the fraction reaching it.

Profile each cap at 1920×1080, 1×, hard shadows, with the same warmup/sample
policy and no competing owned GPU job. Record median/p95, not an assumed 2×/4×
cost: many rays already exit early, while loop/register effects still need
measurement. Retain an opaque control to catch shader-wide regressions.

Selection rule: choose 8 if it meets the gates and looks stable. Promote 16 in
new presets only if 8 fails the cap gate or 16 provides a meaningful visible
benefit at acceptable cost. If neither meets the quality/cost boundary, retain
the experimental label and complete the environment study with that limitation.
The existing glass-only 8 ms median target remains a provisional ceiling on
the current RTX 5070 Ti, separate from cloud/environment cost.

## Environment route and lighting contract

Add a separate opt-in route, proposed name `--lighting material-atmosphere`.
Keep legacy `atmosphere`, fixed `material-study` and diagnostic
`material-furnace` behavior/defaults intact. New atmosphere presentation presets
reuse the accepted material-study scene files rather than regenerating them.

The new route uses:

1. The shared atmosphere state for visible sky, primary-light direction/color/
   intensity and diffuse SH. Remove the fixed study key in this route.
2. Shared atmosphere/cloud probes for roughness-prefiltered specular IBL and
   the existing glass environment lookups. Retain the shared DFG resource.
3. One diffuse source. Shared atmosphere SH is already Lambert-normalized by
   pi; multiply the response by albedo with the material's existing Fresnel/
   rough-diffuse treatment. Do not add the generated diffuse cube or divide
   the SH response by pi again. The legacy SDF shader currently has an extra
   division; preserve that old appearance in its own route and qualify the
   new route against the shared normalization contract.
4. The accepted Oren–Nayar direct term and documented conservative environment
   approximation for terracotta. No new material-response tuning for a sunset.

Use a single canonical sun/environment configuration. Reuse shared config
adapters or a narrow schema surface; do not compose duplicate sun options with
conflicting aliases. Keep manual sun elevation/azimuth useful in the new route
and leave them disabled for the fixed study route. Below-horizon sun values
must pass through shared atmosphere semantics rather than a local fake key.

Reuse the shared view-ray/horizon helpers. Define the probe/view anchor and
meter-to-kilometer mapping explicitly: model-scale orbit movement is not a
change to planetary radius. A studio-sized subject uses the same weather/time
state and local environment anchor throughout its orbit.

## Probe lifetime, readiness and coherence

Create atmosphere/probe resources only for the new route, with the normal frame
slot lifetime and teardown rules. No full glTF renderer migration is required.
SDF's material shader remains its own consumer of shared environment products.

- Consume previous/current probe bindings, actual mip counts and blend state,
  following [probe coherence](dynamic-environment-probe-coherence.md). Do not
  bind an in-progress face set, hardcode five mips for every source, or refresh
  one face from a different solar/weather state than the others.
- Start from a bounded 128-pixel cube-face candidate with five roughness levels;
  retain a 64-pixel comparison for setup/quality cost. Use the shared filtering
  implementation. No resolution sweep beyond those two candidates in this pass.
- A fixed sun/weather state settles once and is reused while the camera or
  material changes. Freeze cloud elapsed time and seed for deterministic
  captures. No animated day/night or weather system is part of this pass.
- Headless capture waits for valid, complete sky/cloud products at the requested
  state and settled transition weights before capturing or profiling. Use
  readiness/submission state, not a guessed sleep or a fallback-colored frame.
  Freeze the weather's elapsed time without freezing resource-completion and
  probe-transition progress. Record setup/settling time separately.
- An interactive sun edit refreshes only the environment. It must retain the
  SDF field. Resize, minimize/recreate and in-flight descriptor/resource reuse
  follow existing shared runtime policies. Route selection may remain startup-
  only; a live environment-browser UI is unnecessary.

## Clouds and foreground composition

Add one optional shared surface-cloud setup after clear-sky material lighting
works. Use an existing broken-cumulus weather preset and bounded shared quality;
freeze time/seed and allow one composition/density adjustment if necessary.

Visible clouds and reflected clouds must use the same weather, sun, time and
anchor. The visible view product and filtered reflection cache have different
sampling/resolution; semantic coherence does not mean pixel-identical detail.

Use the shared external-background/scene-depth composite before HDR post.
SDF already writes foreground depth, including the first glass interface.
Keep clouds behind that depth: the glass shader already includes clouded
environment refraction, so the background composite must not paint clouds over
the glass a second time. Both opaque and glass silhouette edges need checks.

Diffuse SH remains the shared clear-sky approximation; this pass does not
produce full cloud-convolved diffuse lighting or volumetric cloud shadows on
the sculpture. Avoid presenting dense overcast weather with an unrelated
strong unattenuated sun. The selected broken-cloud composition keeps a clear
sun opening; receiver-plane cloud-shadow integration is deferred.

Do not add terrain, a ground plane, local reflection geometry, planar
reflections, SDF-to-SDF reflections, caustics, nested media or new TIR bounces.
The existing homogeneous glass path samples the distant environment only.

## Small visual audition

| Preset | Environment | Intent |
| --- | --- | --- |
| Fixed reference | Accepted generated environment | Retain the material-study baseline |
| Clear low sun | Shared atmosphere, sun initially around +4 degrees, no clouds | Warm side/rim light with cool sky fill and readable carved form |
| Broken-cloud low sun | Same atmosphere/sun plus frozen broken cumulus | Add bright/dark reflection structure and visible cloud edges |

These are three final candidates, not a dawn/dusk/weather preset collection.
Use a clear-sky daytime state as an integration diagnostic before the art
audition; it need not become another showcase preset. No deep-night/celestial
work in this pass.

Keep the same cameras and materials across environments. Start at the existing
exposure; allow one explicitly recorded exposure value per environment if
needed for dusk readability, never a different exposure or fill per material.
Retain a neutral-sphere calibration capture at common exposure to make that
choice inspectable. Keep ACES/post behavior and hard self-shadows unchanged.

Audition bronze, glass, marble and terracotta on the sphere and guardian:
24 subject/material/environment cases, with hero/detail stills (48 images).
Publish at most six short guardian orbits: bronze and glass for each
environment, with glass qualification labels attached to the chosen budget.
The settled first material report still covers all seven catalog materials.

Allow one initial environment audition and one bounded correction of sun
placement, cloud framing or per-environment exposure. No material retuning,
new geometry, repeated HDR hunts or cloud-model look-development loop.

## Acceptance and cost evidence

- Old unselected routes and default four-crossing compatibility controls retain
  their appearance. Report PNG identity or decoded differences explicitly;
  never describe an unmeasured mode as unchanged. New options reject invalid
  values and incompatible clouds/lighting combinations descriptively.
- Material/geometry records, source hashes and the guardian field stay fixed.
  Camera, material selection, crossing budget, sun/weather edits and settled
  cloud animation-off state do not rebuild distance bricks.
- Verify sun/highlight orientation on a neutral/chrome sphere, sky/probe
  horizon orientation and absence of cubemap seams. Check correct diffuse
  normalization and no double ambient term using existing furnace/shared
  lighting controls. Exercise both clear and clouded probe fallback/readiness.
- Recheck sphere/internal rays, separated solids/cavity and chosen guardian
  crossing gate. Inspect glass against the structured sky: more crossings
  may reveal remaining TIR/environment approximation artifacts even if the
  numeric cap gate passes. No nonfinite, exhaustion or self-hit failures.
- Native window checks cover sun changes, resize, material controls and
  environment refresh without a geometry rebuild. A frozen environment
  must not recapture every camera frame or reset on every material change.
- Keep setup/first-ready time, cold SDF build, steady SDF shading/post, cloud
  view/composite and probe-update costs separate. Profile full scene GPU
  median/p95 at 1080p/1×, with all regular passes included. A provisional
  whole-scene target is 16.7 ms; startup and first probe generation are reported
  separately rather than hidden in the steady number. A miss yields a bounded
  quality choice or an explicit experimental/cloud-cost verdict, not an
  optimization project or a changed gate after measurement.
- Publish a compact report with original/new environments, selected material/
  subject, exposure, glass budget, probe source/quality, cap/TIR diagnostics,
  native movies, source/config receipts and costs. Verify served bytes and
  try Carbon/browser checks when reachable; keep unavailable checks distinct
  from successful native/server evidence. Keep the published bundle below
  100 MiB: include the useful selected/worst diagnostic images and full numeric
  receipts, with redundant auxiliary captures retained locally or archived.

## Implementation checkpoints for the next loop

1. Commit this reviewed plan when implementation starts. Preserve current
   captures, configs and native artifact identities. Add/qualify the render-
   quality crossing option and complete the 4/8/16 comparison. Commit.
2. Add the opt-in clear-sky material route, shared probe lifetime/blending,
   primary light and normalized diffuse SH. Qualify old-route compatibility,
   source orientation, readiness, caches and a clear-sky daylight control.
   Commit before adding clouds.
3. Consume the existing cloud view/reflection runtime and depth-aware shared
   composition. Qualify frozen-state reuse, glass silhouette composition,
   view/probe coherence, resize and costs. Commit the integration or its
   explicit bounded limitation.
4. Run the three-environment audition within the look budget, record selected
   settings and glass verdict, capture the bounded matrix and fresh sequential
   profiles, publish one comparison report, update the SDF guide and commit the
   closeout. Commits remain local unless the user separately requests a push.
