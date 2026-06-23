# Moon Surface Detail Captures

Generated on 2026-06-22 under `outputs/sky-moon-surface-detail-001/` and
`outputs/sky-moon-sphere-debug-001/`. The PNGs are ignored by git; this note
records the capture sets used after routing visible moon geometry and moon
debug views to the generated spherical lunar surface map.

## Current Sphere Debug Capture

```sh
mkdir -p outputs/sky-moon-sphere-debug-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view moon-surface --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-moon-sphere-debug-001/atmosphere-moon-surface-sphere.png
```

## Previous Routing Captures

```sh
mkdir -p outputs/sky-moon-surface-detail-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --time-hours 12 --sun-azimuth-offset -180 --moon-size-scale 8 --moon-intensity 4 --pause-time --no-reference-geometry --output outputs/sky-moon-surface-detail-001/atmosphere-moonlit-surface-map-readable.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 88 --planet-time-hours 18.13 --planet-camera-mode orbit --output outputs/sky-moon-surface-detail-001/planet-moon-occlusion-surface-map.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/sky-moon-surface-detail-001/planet-day-moon-surface-map-washout.png
./build/dev/projects/fluid/fire_3d/fire_3d --headless --frames 8 --width 1280 --height 720 --grid-width 64 --grid-height 64 --grid-depth 64 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --pause-time --output outputs/sky-moon-surface-detail-001/fire-3d-atmosphere-moon-surface-map.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 8 --width 1280 --height 720 --grid-width 64 --grid-height 64 --grid-depth 64 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --pause-time --output outputs/sky-moon-surface-detail-001/explosion-3d-atmosphere-moon-surface-map.png
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 8 --width 1280 --height 720 --grid-width 48 --grid-height 48 --grid-depth 48 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --time-hours 18 --sun-azimuth-offset 0 --moon-size-scale 8 --moon-intensity 4 --pause-time --output outputs/sky-moon-surface-detail-001/water-3d-atmosphere-moon-surface-map-readable.png
```

## Observations

- Standalone atmosphere final view renders the visible moon through geometry
  with the new surface-map binding. The crescent is small, so it validates
  routing and phase behavior more than fine texture quality.
- `moon-surface` debug now renders a centered close-up sphere through
  `CelestialBodyFrame::SurfaceDebug`. It samples the generated
  `LunarSurfaceMap`, not a 2D near-side atlas, and forces base-mip sampling so
  broad maria are not averaged away.
- The sphere debug capture is intentionally neutral-exposure and contrast-tuned
  for surface inspection, not representative final-scene exposure.
- Fire, explosion, and water still draw the geometry moon over their direct
  atmosphere backgrounds. These captures are useful route checks for ray-marched
  and surface-composited scenes.
- Planet moon captures remain useful for occlusion and daytime washout, but the
  moon is not large enough in these frames to judge surface detail.
- The `lunar-surface-map-v2` capture makes the maria read as broad dark basaltic
  plains. Remaining visual tuning is mostly crater scale, mare edge softness,
  and final-scene lighting balance.
