# Fire 3D captures

## Rolling burner

### Fixed dusk capture

The showcase film uses an open-top 256 × 512 × 256 material grid and
128 × 256 × 128 flow. The sun holds at 18:00 throughout the preroll and the
eight-second edit. The upper wisp stays visible against the evening sky as
the taller flame tongue lights the underside of the smoke.

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --fps 60 --frames 660 \
    --width 1920 --height 1080 --anti-aliasing off --fire-burner rolling --pyro-firelight \
    --grid-width 256 --grid-height 512 --grid-depth 256 --pyro-tall-domain \
    --pyro-coarse-flow --pyro-ray-skipping --capture-camera-distance 2.04 \
    --capture-target-height 1.03 --capture-video-orbit-degrees 0 --time-of-day-mode solar \
    --time-hours 18 --time-speed-hours-per-second 0 --require-validation --output /tmp/fire-rolling-dusk.mp4
```

The edit drops the first 180 frames, then keeps every second frame at 30 fps.
The camera holds for the first 2.5 seconds, then eases to 1.28× magnification
at 7.5 seconds using cubic smoothstep. The crop is anchored at 50% across and
90% down the 1920 × 1080 master for the 1280 × 720 film. The poster uses
frame 198 (6.6 seconds), where the two flame tongues separate, and the hover
preview uses the final 3.5 seconds. The [media receipt](../../../../site/media/receipts/fire-3d-rolling.json)
records the native inputs and encoding commands.

### Earlier Rolling comparisons

The first wide shot placed the camera at distance `2.75` and target height
`0.5`. A starting time of `12.875` and speed of `0.875` hours per second
gave the retained film its 15:30–22:30 light progression. The firelight
comparison used `17.15` and `0.1` for a 17:27–18:15 sweep. At 7.5 seconds
the smoke fold on the left catches light from the taller flame tongue.

The closer framing kept the tall opening wisp in view and moved the burner
toward the lower edge. At this framing, a slower drift starting at `17.9125`
and advancing `0.0125` hours per second still took the horizon from gold to
red over the retained 17:57–18:03 window. The fixed dawn comparison used
`--time-hours 5.8 --time-speed-hours-per-second 0`. The selected dusk shot
keeps the fire burning in the light just before nightfall.

## Earlier Balanced capture

The capture used one source, no obstacle, terrain or clouds, camera distance
`1.65`, three seconds of hidden warm-up, and the retained 15:30-to-22:30 window.
To replay its Balanced overrides with the current executable, select the Classic burner:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --fire-burner classic --headless --capture video --frames 660 --fps 60 --width 1280 --height 720 --capture-video-orbit-degrees 0 --capture-camera-distance 1.65 --pyro-sources 1 --pyro-obstacle-radius 0 --time-of-day-mode solar --time-hours 12.875 --time-speed-hours-per-second 0.875 --pyro-source-radius 0.11 --pyro-soot 13 --pyro-soot-yield 0.35 --pyro-expansion 1.10 --pyro-flame-cooling 3.4 --pyro-shredding 4.2 --pyro-turbulence 1.2 --pyro-buoyancy 2.1 --pyro-source-force 8.0 --output outputs/showcase/audition-2/pyro/fire-3d-refine-sim-balanced-source.mp4
```

The [media manifest](../../../../docs/media/showcase/manifest.json) retains the
frame-180 trim and publication provenance.
