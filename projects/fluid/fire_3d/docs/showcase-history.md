# Fire 3D day-to-night capture record

This page retains the source command for the earlier fire-and-smoke clip. The
current native simulation and renderer are documented in the
[Fire 3D guide](../README.md); the poster, film, trim, hash and provenance remain
in the [showcase archive package](../../../../docs/media/showcase/README.md).

The capture used one source, no obstacle, terrain or clouds, camera distance
`1.65`, three seconds of hidden warm-up, and the retained 15:30-to-22:30 window.
Its Balanced overrides are recorded in this source command:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --frames 660 --fps 60 --width 1280 --height 720 --capture-video-orbit-degrees 0 --capture-camera-distance 1.65 --pyro-sources 1 --pyro-obstacle-radius 0 --time-of-day-mode solar --time-hours 12.875 --time-speed-hours-per-second 0.875 --pyro-source-radius 0.11 --pyro-soot 13 --pyro-soot-yield 0.35 --pyro-expansion 1.10 --pyro-flame-cooling 3.4 --pyro-shredding 4.2 --pyro-turbulence 1.2 --pyro-buoyancy 2.1 --pyro-source-force 8.0 --output outputs/showcase/audition-2/pyro/fire-3d-refine-sim-balanced-source.mp4
```

The [media manifest](../../../../docs/media/showcase/manifest.json) retains the
frame-180 trim and publication provenance.
