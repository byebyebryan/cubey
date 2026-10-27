# Water 3D dam-break capture record

This page retains the source command for the earlier dam-break clip. The
current native solver and renderer are documented in the
[Water 3D guide](../README.md); the poster, film, trim, hash and provenance remain
in the [showcase archive package](../../../../docs/media/showcase/README.md).

The capture disables wave forcing so the motion comes from the initial volume.
It uses a `0.60/0.75/0.75` fill, whitewater intensity `1.35`, speed threshold
`0.85`, a 0.5-second pre-roll, camera distance `2.2` and a 30-degree eased arc:

```sh
./build/dev/projects/fluid/water_3d/water_3d --headless --capture video --frames 510 --fps 60 --width 1280 --height 720 --capture-camera-distance 2.2 --capture-video-orbit-degrees 30 --time-of-day-mode solar --time-hours 13.5625 --time-speed-hours-per-second 0.875 --no-clouds --cloud-weather-preset broken-cumulus --cloud-quality full --no-water3d-wave --water3d-whitewater --water3d-whitewater-intensity 1.35 --water3d-whitewater-speed-threshold 0.85 --water3d-initial-fill-width 0.60 --water3d-initial-fill-height 0.75 --water3d-initial-fill-depth 0.75 --output outputs/showcase/audition-2/water/water-3d-refine-dam-only-fill-60-75-75-no-clouds-preroll-source.mp4
```

The [media manifest](../../../../docs/media/showcase/manifest.json) retains its
trim and publication provenance.
