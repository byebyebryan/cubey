# Moon Surface Detail Captures

Generated on 2026-06-22 and refreshed on 2026-06-23 under
`outputs/sky-moon-surface-detail-001/`, `outputs/sky-moon-sphere-debug-001/`,
and `outputs/sky-moon-reference-001/`.
The PNGs are ignored by git; this note records the capture and reference sets
used after routing visible moon geometry and moon debug views to the generated
spherical lunar surface map.

## References

```sh
mkdir -p outputs/sky-moon-reference-001
curl -L https://lroc.im-ldi.com/news/uploads/lroc_wac_nearside_noslew.png -o outputs/sky-moon-reference-001/lroc-wac-nearside.png
curl -L https://lroc.im-ldi.com/news/uploads/lroc_wac_nearside_noslew_anot.png -o outputs/sky-moon-reference-001/lroc-wac-nearside-annotated.png
curl -L https://astrogeology.usgs.gov/ckan/dataset/db948a2d-4d6a-4775-a0d3-12613d36f9e7/resource/d24d5ef3-abc5-42ee-ac7c-4c3261106327/download/moon_lro_lroc-wac_mosaic_global_1024.jpg -o outputs/sky-moon-reference-001/usgs-lroc-wac-global-1024.jpg
```

## Current Sphere Debug Capture

```sh
mkdir -p outputs/sky-moon-sphere-debug-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view moon-surface --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-moon-sphere-debug-001/atmosphere-moon-surface-sphere.png
```

## Current Moon-View Checks

```sh
mkdir -p outputs/sky-moon-surface-detail-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view moon --atmosphere-preset moonlit-night --moon-size-scale 8 --moon-intensity 0.2 --pause-time --no-reference-geometry --output outputs/sky-moon-surface-detail-001/atmosphere-moonlit-v15-debug-moon-full.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-moon-surface-detail-001/atmosphere-moonlit-v15-normal.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --time-hours 12 --sun-azimuth-offset -180 --moon-size-scale 8 --moon-intensity 4 --pause-time --no-reference-geometry --output outputs/sky-moon-surface-detail-001/atmosphere-moonlit-v15-readable.png
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
- The `lunar-surface-map-v16` capture keeps the generated mare field procedural
  and body-space, but rotates the mare noise domain and applies a modest
  nearside bias so the broad dark plains land closer to the stable face that
  the geometry moon presents. Compared with v12, it widens the mare fill curve
  and gives the lowest-frequency mare mass more weight, so the dark basalt-like
  plains occupy more of the readable disk. Compared with v13, it applies a
  small front-axis presentation rotation so the larger basins sit more naturally
  on the visible face. Compared with v14, it grows a broad central-basin
  contribution and fades limb maria harder so the largest readable dark mass is
  on the face rather than leaving a small center basin and a larger edge basin.
  V16 adds more medium/small crater texture and gentler packed normal relief so
  terminator detail reads without turning the close-up debug view into sparkle.
  The goal is far-field moon readability: broader dark plains plus independent
  surface variation, without screen-space texture swimming, equirectangular UV
  shaping, visible overlapping stamp centers, or the earlier dragged/smeared
  gradients.
- The normal `moonlit-night` capture is useful as a routing check but does not
  frame the moon well for texture review. The enlarged crescent capture shows
  final-view binding and phase behavior; the sphere debug capture remains the
  controlled full-disk texture check.
- The `moon` debug view now frames and front-lights the geometry moon for a
  small-disk material check. Use low moon intensity for this capture so the
  texture does not clip white.
- Remaining visual tuning is mostly final-scene moon-size contrast and
  capture-driven material balance.
