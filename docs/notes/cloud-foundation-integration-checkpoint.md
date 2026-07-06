# Cloud Foundation Integration Checkpoint

This note checkpoints the integration scope that promoted the former
`projects/cloud` standalone renderer into a shared weather layer. Current cloud
tuning happens through `projects/atmosphere`; ocean consumes the accepted
surface-view path in final sky, and planet consumes the shared runtime as an
opt-in integration target.

## Batch Scope

- Promote reusable cloud contracts for cloud radiance/transmittance, metadata,
  low-frequency shadowing, and a future reflection contribution.
- Keep `projects/atmosphere` as the primary tuning surface while moving stable
  render vocabulary and shader assets into shared engine/render locations.
- Integrate clouded sky first through the atmosphere testbed, then let ocean
  consume the surface-view sky composite without pushing clouds into the water
  material yet.
- Do not integrate planet in this batch. Current planet sky/horizon issues are
  tracked separately in `docs/notes/sky-celestial-current-state.md`.
- Do not build a dynamic cloud reflection cubemap in this batch. The reflection
  surface is an API slot only until backdrop and shadow output are stable.

## Acceptance

- Cloud captures continue to render through the same visual model after the
  standalone app is absorbed.
- Shared cloud contracts are available without depending on a standalone cloud
  project.
- Atmosphere can render a clouded-sky path without folding cloud noise into the
  clear-sky atmosphere shader.
- Ocean can render shared clouds in the sky/background pass without raymarching
  clouds in the water shader.

## Landed Status

- Shared cloud vocabulary now lives in `cubey::render` through
  `include/cubey/render/cloud_layer.h` and `src/cubey/render/cloud_layer.cpp`.
  The promoted surface covers quality, sampling, distance/orbit modes, debug
  view enums, frame uniforms, temporal uniforms, generated resource helpers,
  cloud product metadata, a shadow product slot, and a placeholder reflection
  contribution slot.
- Cloud shaders moved under `shaders/cubey/cloud/`. `projects/atmosphere` is
  now the primary tuning surface, while generated cloud resources,
  march/temporal/composite materials and pipelines, render-graph product
  declaration, descriptor updates, and temporal history are owned by
  `cubey::render::CloudLayerRuntime`.
- `projects/atmosphere` now builds the shared cloud shaders, consumes
  `CloudLayerRuntime` in external-background mode, composites clouds over the
  clear-sky atmosphere in final view, and exposes a collapsed Clouds control
  group backed by the same `clouds.*` run-config overrides used by the
  standalone cloud project.
- `projects/ocean` now compiles the shared cloud shaders, owns a
  `CloudLayerRuntime`, and composites surface-volume clouds over the atmosphere
  sky in final view by default. The cloud product uses ocean scene depth for sky
  composition and the shared atmosphere sun/moon/ambient lighting state.
  `--no-clouds` keeps the clear-sky A/B path. Ocean still has the older
  cloud-shadow diagnostic view and controls that consume the shared
  `CloudLayerShadowProduct` shape; that shadow remains procedural and local to
  ocean.
- `projects/planet` now has an opt-in `--clouds` path that composites the shared
  cloud product over the planet HDR scene using scene depth. It is an integration
  checkpoint, not a finished cloud shadow/reflection/environment-lighting path.

## Remaining Gaps

- The shared runtime is still a render-layer helper, not a full environment
  system. It does not yet output production cloud shadows, reflection probes, or
  clouded environment lighting.
- Atmosphere has runtime-backed cloud controls and can enable temporal resolve,
  but cloud debug-view surfacing, lighting feedback into the atmosphere, and
  cloud-driven environment/reflection outputs remain deferred.
- Ocean clouds are sky/background composition only. Ocean does not sample a real
  cloud shadow texture, cloud reflection product, or clouded sky-probe
  contribution in the water material yet.
- Planet cloud shadows, reflections, and final high-oblique/orbit transition
  polish remain deferred until the active planet sky/horizon issues are
  stabilized.

## Runtime Promotion Checkpoint

The reusable cloud runtime has landed without intentionally changing the cloud
look during promotion. `projects/atmosphere` now owns cloud-specific review
captures and visual tuning, and planet proves that a consumer can reuse the same
cloud renderer without copying descriptor or temporal-history code.

Still out of scope: production cloud shadow textures for ocean/terrain/planet,
cloud reflection or sky-probe contribution, clouded lighting feedback, and
visual retuning beyond integration fixes.

## High-Oblique Baseline 2026-06-28

Current absorbed-cloud review pack:

```sh
projects/atmosphere/capture_cloud_review.sh outputs/atmosphere-cloud-review-current
```

The pack now defaults to 1920x1080 full-quality output and includes a
`high-oblique-no-clouds` comparison beside `high-oblique-final`. The no-cloud
frame confirms the clear atmosphere/reference horizon is not the source of the
remaining high-oblique roughness; the issue is in the cloud handoff itself.

Current read:

- surface horizon/upward views remain the strongest baseline for local
  volumetric shape;
- high-oblique final has usable foreground clouds, but the far cloud band still
  reads harder and noisier than the clear-sky comparison;
- orbit-shell oblique is useful for shell/weather diagnostics, but it remains a
  flatter cloud-top representation rather than the desired high-oblique handoff;
- future work should keep the high-oblique comparison in the acceptance pack
  before promoting cloud shadows/reflections or planet-scale consumers.

## Surface Reference Reboot 2026-07-01

`projects/cloud_ref` is now locked as a narrow surface-view/local-volume
reference. It is useful because its surface cloud shape, deterministic Bayer
sampling, terrain-post resolve, lower ceiling, and optical-depth lighting give a
stable local signal with much less visible edge noise than the absorbed
foundation cloud path.

It is not the high-altitude or orbit target. Its camera-following cloud dome,
standalone sky/water context, and horizon masking are deliberate limitations
that should not be copied as production architecture.

The production reboot happens in the shared cloud layer through
`projects/atmosphere`. Use:

```sh
projects/atmosphere/capture_cloud_ref_parity.sh outputs/cloud-ref-parity-current
```

as the comparison pack before changing shared cloud lighting or horizon
handoff. The pack forces atmosphere into full-quality, local-only,
`cloud-ref-compatible` mode with temporal off, Bayer sampling, terrain-post
resolve, and no far-horizon cloud layer so shader and lighting differences stay
visible. Surface noon/backlit parity is the first acceptance target; dawn,
dusk, and night checks make sure the shared environment lighting contract does
not keep clouds sunlit after the sun is below the horizon.

## Reference-Parity Implementation Checkpoint 2026-07-01

The shared atmosphere path now has an explicit `reference-parity` cloud preset
that selects the local `cloud-ref-compatible` renderer rather than the high or
orbit cloud paths. This is intended as a narrow surface-cloud testbed, not a
replacement for the later high-oblique/orbit solution.

The shared cloud frame also receives resolved environment lighting from
`AtmosphereEnvironmentLighting`: separate sun, moon, and ambient terms are now
packed into cloud uniforms instead of assuming a fixed sun-only input. This
keeps the parity path compatible with the day/night environment contract before
cloud lighting is promoted to a fuller production model.

`cloud-ref-compatible` now uses the same local idea as `projects/cloud_ref` for
direct lighting: march light optical depth, integrate view transmittance through
optical thickness, keep deterministic single-frame sampling, and rely on the
terrain-post resolve for the final edge cleanup. The current capture pack is:

```sh
projects/atmosphere/capture_cloud_ref_parity.sh outputs/cloud-ref-parity-current
```

Current read:

- atmosphere surface-up/noon captures now recover the high-frequency local cloud
  shape closely enough to use as the next shared-cloud tuning baseline;
- `cloud_ref` remains the cleaner surface/horizon reference because it owns its
  own dome, water, and horizon masking;
- atmosphere horizon captures still expose integration artifacts, especially the
  hard horizon handoff and night/twilight contrast, so horizon polish is not
  accepted yet;
- high-oblique/orbit weather remains deliberately out of scope for this parity
  step.

## Surface Lighting Regime Checkpoint 2026-07-04

The `reference-parity` path now treats surface/night lighting as a separate
regime instead of allowing moonlit ambient and post contrast to reuse the same
strength as daylight. This removes the obvious blue-white cloud silhouette that
showed up at night while keeping stars and moonlight visible in the background.

The local `cloud-ref-compatible` march also has a surface-only grazing horizon
fade. This is not an orbit/high-oblique cloud solution; it is a scoped handoff
for the clean surface reference mode, where the cloud dome can otherwise expose
hard local-volume edges near the horizon. The fade works even when the optional
far-horizon layer is disabled by `reference-parity`.

Use this capture pack for future lighting changes:

```sh
projects/atmosphere/capture_cloud_lighting_regimes.sh outputs/cloud-lighting-regimes-current
```

Current read:

- surface-up noon remains the baseline for local cloud shape and high-frequency
  detail;
- surface-up night no longer reads as sunlit clouds against a dark sky;
- twilight is intentionally softer than noon, but should not wash the cloud
  layer into a flat pale blanket;
- horizon captures are diagnostic only for this surface reference pass. A
  production high-oblique/orbit handoff still needs separate treatment.

The shared twilight pass adds three explicit controls:
`clouds.twilight_color_strength` boosts low-sun ambient color from the horizon
sky/sun radiance, `clouds.twilight_edge_strength` boosts optical-edge rim color
in the `cloud-ref-compatible` marcher, and
`clouds.twilight_saturation_strength` lets final composite saturation treat
twilight as its own regime instead of collapsing toward the night grade. These
are color-response controls only; they do not change cloud density, weather
placement, horizon handoff, or orbit/high-oblique cloud topology.

Use the lighting-regime capture pack before retuning those values. Noon should
stay the shape/detail reference, twilight should gain warmer cloud edges and
less grey desaturation, and night should remain mostly silhouette/moonlight
without reintroducing daylight-colored clouds.

`clouds.afterglow_strength` is the optional beauty control for a stronger
afterglow look: red, pink, or purple accents near the low sun. It is intentionally
subtle by default and is applied through top/edge/rim cloud lighting rather than
as a global post tint. The lighting-regime capture pack now includes an
`afterglow` showcase row using a stronger temporary value so the accent can be
reviewed without changing the normal twilight default.

## Surface Horizon Handoff Target 2026-07-04

The current `reference-parity` preset is deliberately local-only: it disables
automatic distance selection and the optional far-horizon layer so the
`cloud-ref-compatible` surface volume stays easy to compare with
`projects/cloud_ref`. That also means the horizon rows in the lighting-regime
pack expose the local-volume fade by itself. They should not be treated as proof
that the production horizon bridge is working.

The next scoped target is the surface lower-sky handoff in
`projects/atmosphere`: compare local-only reference parity against the same
preset with `--cloud-distance-mode auto --cloud-horizon-layer`, then tune the
existing integrated horizon bridge so it softly replaces distant grazing local
samples. This is not a new cloud color model and not the high-oblique/orbit
weather solution. Near and upward local clouds should stay visually unchanged.

Implementation note: `cloud-ref-compatible` originally returned from the local
march path before horizon diagnostics or the integrated bridge could run, so
explicit `auto`/horizon overrides on the `reference-parity` preset were not
actually exercising the bridge. The compatible path now keeps the local march as
the foreground and composes the same integrated horizon layer behind it only
when the distance mode and horizon layer are explicitly enabled. The far bridge
uses a broader, prefiltered version of the ref-compatible density so horizon
continuity remains low-detail and sky-biased instead of becoming another noisy
local march.

Use this review pack for the surface horizon handoff:

```sh
projects/atmosphere/capture_cloud_surface_horizon_regimes.sh outputs/cloud-surface-horizon-handoff
```

The handoff should be visible in `horizon-handoff` and
`integrated-horizon-alpha`, while final color remains subtle. If a hard band is
also present in the no-cloud row, treat it as a sky/background horizon issue
rather than a cloud bridge regression.

## Surface Volume Promotion 2026-07-05

The surface/local cloud path is now accepted as the shared foundation baseline.
The implementation should stop presenting it as `cloud-ref-compatible` except
for config compatibility; the production name is `surface-volume`.

The promotion deliberately does not declare aerial, high-oblique, or orbit
clouds solved. The previous procedural shared-cloud path carried the auto/orbit
branch, but it also carried the edge-noise and lighting problems that forced the
reference reboot. Keep that code reachable only as explicit
`experimental-aerial-orbit` scaffolding until a new aerial bridge can use the
surface-volume local signal without regressing surface quality.

Current split:

- `surface-volume`: production surface/local clouds, tuned through
  `projects/atmosphere` and cross-checked against `projects/cloud_ref`;
- `surface_cloud_march.comp`: lean shared shader for the production
  surface-volume path;
- `cloud_march.comp`: general shared shader for aerial/orbit, far-bridge, and
  diagnostic paths;
- `experimental-aerial-orbit`: temporary high/orbit transition scaffold, not a
  fallback for surface rendering;
- `projects/cloud_ref`: known-good narrow reference/demo that should remain
  available;
- `projects/clouds_legacy` and git history: record of failed standalone and
  absorbed approaches, not active implementation targets.
