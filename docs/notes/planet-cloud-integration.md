# Planet Cloud Integration Checkpoint

This note tracks the first planet-specific pass after the shared cloud runtime
was integrated as an opt-in layer behind `--clouds`.

## Baseline

Baseline capture:

```sh
WIDTH=1280 HEIGHT=720 FRAMES=2 QUALITY=quarter \
  projects/planet/capture_cloud_review.sh outputs/planet-cloud-review-baseline
```

The generated images remain untracked. The contact sheet showed these active
integration issues:

- Surface and high-altitude composition has no scene-depth occlusion, so clouds
  can read as a sky overlay instead of terrain-aware atmosphere.
- The high transition view has useful cloud mass, but the handoff still reads as
  a separate high-altitude cloud layer near the horizon.
- Orbit clouds have visible sparse detail, but still need tuning before they are
  a convincing planet-scale weather read.
- The surface diagnostics in this review pack are low-signal; they are still
  useful for smoke coverage, but not enough for judging final surface cloud
  shape alone.

## Current Scope

The next pass should keep planet clouds opt-in, add scene-depth-aware cloud
composition for planet, and retune the planet-specific distance regimes. Cloud
shadows, ocean/terrain reflections, and production temporal stabilization remain
deferred until composition and scale reads are stable.

## Depth Composite Pass

Follow-up capture:

```sh
WIDTH=1280 HEIGHT=720 FRAMES=2 QUALITY=quarter \
  projects/planet/capture_cloud_review.sh outputs/planet-cloud-review-current
```

The pass added a planet-only cloud composite shader that samples scene depth and
suppresses final cloud contribution when opaque scene depth is closer than the
cloud hit distance. Planet also opts its forward depth attachment into sampled
usage; other forward-pass users keep non-sampled depth by default.

The planet `--clouds` defaults now use quarter resolution, no temporal resolve,
automatic local/orbit distance handling, and a wider high-altitude transition.
The after capture has more usable dawn/orbit cloud fill and keeps high
transition cloud mass readable. Remaining visual work is still horizon polish:
high-oblique clouds and the sky/terrain boundary can read as separate layers
even when depth overdraw is masked.

## High-Oblique Review

Diagnostic capture:

```sh
WIDTH=1280 HEIGHT=720 FRAMES=2 QUALITY=quarter \
  projects/planet/capture_cloud_review.sh outputs/planet-cloud-review-oblique-grazing
```

This pass expanded the review pack with low-surface horizon/overhead views,
60 km / 140 km / 260 km transition samples, transition-weight diagnostics, and
a `scene-depth-occlusion` debug view. The shader now boosts the orbit-shell
handoff for elevated, grazing rays while leaving downward and low-altitude rays
on the local volume path longer.

The captures show the 140 km limb now entering the orbit-shell regime instead
of staying mostly local-volume. That improves the handoff shape, but the visible
chunkiness at quarter and half quality points to remaining orbit-shell
filtering/detail work rather than just a distance-threshold problem. The next
cloud/planet pass should focus on shell detail antialiasing, horizon-limb
filtering, and higher-signal surface diagnostics before treating the transition
as visually done.

## Orbit Shell Filtering Pass

Full-quality capture:

```sh
WIDTH=1920 HEIGHT=1080 FRAMES=2 QUALITY=full \
  projects/planet/capture_cloud_review.sh outputs/planet-cloud-review-shell-filtering
```

The pass added orbit-shell footprint/filter/mass diagnostics and routes the
surface-shell renderer through one shared footprint value for detail visibility,
filtered mass, height/normal sampling, and limb alpha. The shell now filters
micro/detail fields more aggressively at grazing angles, gates low-mass alpha
near the limb, and fades fully grazing shell color more strongly.

The full-quality captures show the high-oblique shell edge is less stippled
without removing the main cloud bodies or orbit-scale wisps. A `--no-clouds`
comparison at 140 km also confirmed that part of the hard high-oblique horizon
read comes from the planet/sky/surface silhouette itself, not only cloud-shell
alpha. Remaining work should therefore split into two tracks: further shell
texture quality for orbit-scale weather, and separate planet horizon/surface
composition cleanup.
