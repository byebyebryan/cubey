# Geometry Moon Migration Captures

Generated on 2026-06-22 under `outputs/sky-moon-geo-migration-001/`. The PNGs
and contact sheets are ignored by git; this note records the exact capture set
used after moving migrated visible moon paths to `CelestialBodyFrame` geometry.

## Captures

```sh
mkdir -p outputs/sky-moon-geo-migration-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --time-hours 12 --sun-azimuth-offset -180 --moon-size-scale 8 --moon-intensity 4 --pause-time --no-reference-geometry --output outputs/sky-moon-geo-migration-001/atmosphere-moonlit-geometry-readable.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view moon-surface --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-moon-geo-migration-001/atmosphere-moon-surface-debug.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 88 --planet-time-hours 18.13 --planet-camera-mode orbit --output outputs/sky-moon-geo-migration-001/planet-moon-occlusion.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/sky-moon-geo-migration-001/planet-day-moon-washout.png
./build/dev/projects/fluid/fire_3d/fire_3d --headless --frames 8 --width 1280 --height 720 --grid-width 64 --grid-height 64 --grid-depth 64 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --pause-time --output outputs/sky-moon-geo-migration-001/fire-3d-atmosphere-moon.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 8 --width 1280 --height 720 --grid-width 64 --grid-height 64 --grid-depth 64 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --pause-time --output outputs/sky-moon-geo-migration-001/explosion-3d-atmosphere-moon.png
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 8 --width 1280 --height 720 --grid-width 48 --grid-height 48 --grid-depth 48 --pbr-environment-source atmosphere --atmosphere-preset moonlit-night --time-hours 18 --sun-azimuth-offset 0 --moon-size-scale 8 --moon-intensity 4 --pause-time --output outputs/sky-moon-geo-migration-001/water-3d-atmosphere-moon-readable.png
```

The `*-readable` captures intentionally boost moon size/intensity or choose a
moon-facing azimuth/time so the geometry path is visible in thumbnails. The
natural water capture is also kept in the folder as
`water-3d-atmosphere-moon.png`.

## Observations

- Standalone atmosphere final view renders a crescent moon through geometry. At
  the time of this capture, `moon-surface` debug still exercised the inline
  shader atlas view; that has since been superseded by the sphere debug capture
  in `outputs/sky-moon-sphere-debug-001/`.
- Planet moon occlusion and day-moon washout captures remain consistent with
  the existing depth-tested body path.
- Fire and explosion show the moon geometry over the direct atmosphere
  background before their raymarch content.
- Water needed a different time/offset to frame the moon, but the readable
  capture verifies the geometry path inside the surface scene pass.
