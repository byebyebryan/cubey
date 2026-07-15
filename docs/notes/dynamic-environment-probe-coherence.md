# Dynamic Environment Probe Coherence

## Problem

The procedural sky is evaluated every frame, but reflective materials consume a
filtered cubemap. Updating one cube face per frame exposed six different solar
times at once. The helmet remained opaque, yet its sharp metallic reflections
could wash into the similarly colored background and briefly read as transparent.
Cloud reflections also snapped because every atmosphere change invalidated the
cloud probe's double-buffered interpolation.

## Contract

- Direct atmosphere and cloud composition remain per-frame view products.
- Clear-sky specular IBL uses two prefiltered cubemaps and one working radiance
  cube.
- A capture renders all six radiance faces from one frozen environment state,
  prefilters every face/mip into the inactive cube, then publishes it atomically.
- The default capture cadence is 4 Hz. Repeated environment edits coalesce while
  the previous/current pair crossfades over the same 0.25 second interval.
- An unchanged atmosphere does not refresh periodically.
- `AtmosphereEnvironmentRuntime::advance` advances both clear-sky and cloud
  timelines. Environment changes request a sky capture but do not invalidate
  cloud history.
- Explicit cloud configuration/resource edits may still invalidate the cloud
  probe because they change the represented field, not only its lighting.

Forward PBR consumes the existing previous/current environment bindings. Ocean's
custom material mirrors that contract with separate atmosphere cube descriptors
and a blend uniform. glTF Viewer, ocean, and Water3D no longer advance cloud
probe cadence independently.

## Validation

The focused core, ocean, glTF Viewer, and Water3D builds and smoke tests cover
the runtime and shader contracts. Dynamic dawn captures are kept under the
ignored `outputs/environment-reflection-review/` directory for local comparison.
The ocean GPU trace shows atmosphere captures on frames 0, 8, 16, 24, and so on
at 30 FPS, with about 0.77 ms on capture frames and negligible work between
captures. The observed average over 57 timed frames was 0.109 ms.

This batch deliberately does not retune glTF material response or automatic
exposure. Those remain separate concerns if an authored material still reads too
reflective after probe coherence is stable.
