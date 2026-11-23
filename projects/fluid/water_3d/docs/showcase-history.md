# Water 3D dam-break capture history

The [Water 3D guide](../README.md) covers the native solver and renderer.

## Afternoon capture

The site capture keeps the dam-break fill, disabled wave forcing, whitewater,
half-second pre-roll and eased 30-degree orbit. It starts the solar clock at
13.8125 hours and advances it at 0.375 hours per captured second. The retained
eight seconds run from about 14:00 to 17:00 in clear sky. The
[capture receipt](../../../../site/media/receipts/water-3d-daylight.json) records
the command, matched control and selected web derivatives.

## Earlier day-to-night capture

The [showcase archive package](../../../../docs/media/showcase/README.md)
retains the earlier film, poster, trim and provenance.

The capture disables wave forcing so the motion comes from the initial volume.
It uses a `0.60/0.75/0.75` fill, whitewater intensity `1.35`, speed threshold
`0.85`, a 0.5-second pre-roll, camera distance `2.2` and a 30-degree eased arc:

```sh
./build/dev/projects/fluid/water_3d/water_3d --headless --capture video --frames 510 --fps 60 --width 1280 --height 720 --capture-camera-distance 2.2 --capture-video-orbit-degrees 30 --time-of-day-mode solar --time-hours 13.5625 --time-speed-hours-per-second 0.875 --no-clouds --cloud-weather-preset broken-cumulus --cloud-quality full --no-water3d-wave --water3d-whitewater --water3d-whitewater-intensity 1.35 --water3d-whitewater-speed-threshold 0.85 --water3d-initial-fill-width 0.60 --water3d-initial-fill-height 0.75 --water3d-initial-fill-depth 0.75 --output outputs/showcase/audition-2/water/water-3d-refine-dam-only-fill-60-75-75-no-clouds-preroll-source.mp4
```

The [media manifest](../../../../docs/media/showcase/manifest.json) retains its
trim and publication provenance.
