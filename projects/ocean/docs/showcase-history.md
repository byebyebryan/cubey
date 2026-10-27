# Ocean day-to-night capture record

This page retains the source command for the earlier Windy sea-state film. It
is an immutable capture recipe, not a separate current gallery; the poster,
film, publication hash and provenance remain in the
[showcase archive package](../../../docs/media/showcase/README.md).

The capture used the default camera, no size-reference pillar and a 14:00 to
21:00 solar sweep over 480 frames at 60 FPS:

```sh
./build/dev/projects/ocean/ocean --headless --capture video --frames 480 --fps 60 \
  --width 1280 --height 720 --ocean-sea-state windy \
  --ocean-camera-preset default --capture-video-orbit-degrees 30 \
  --no-ocean-size-reference --time-of-day-mode solar --time-hours 14.0 \
  --time-speed-hours-per-second 0.875 \
  --output outputs/showcase/audition-2/ocean/ocean-day-dusk-night-30deg-60fps-source.mp4
```

The matching [media manifest](../../../docs/media/showcase/manifest.json) records
the published derivative and native source identities.
