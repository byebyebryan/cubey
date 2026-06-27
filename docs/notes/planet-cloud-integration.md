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
