# Planet Visual Capture Recipes

These recipes are meant for repeatable visual checks while the planet project is
still changing quickly. They are not golden-image tests yet; they are a compact
matrix for manually comparing LOD, atmosphere, celestial, and surface-view
behavior after renderer or camera changes.

All commands write under `outputs/`, which is intentionally ignored by git.

## Baseline Views

Use the mean solar clock controls to keep captures deterministic:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode orbit --output outputs/planet-orbit-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode orbit --output outputs/planet-orbit-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode orbit --output outputs/planet-orbit-night.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode surface --output outputs/planet-surface-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/planet-surface-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode surface --output outputs/planet-surface-night.png
```

## Atmosphere Comparison

The default `analytic` mode is the stable project-local sky. The
`physical-preview` mode is an opt-in comparison path for developing a more
physical atmosphere without replacing the default.

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode surface --planet-atmosphere-mode analytic --output outputs/planet-atmo-analytic-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode surface --planet-atmosphere-mode physical-preview --output outputs/planet-atmo-physical-preview-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-atmosphere-mode physical-preview --output outputs/planet-atmo-physical-preview-backlit.png
```

## LOD And Seam Diagnostics

Use these after changing surface planning, terrain scale, patch resolution, or
camera movement:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view lod-level --output outputs/planet-debug-lod-level.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view lod-transition --output outputs/planet-debug-lod-transition.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view seams --output outputs/planet-debug-seams.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view wireframe --output outputs/planet-debug-wireframe.png
```

## Celestial Checks

`celestial-planes` shows the equator, ecliptic, lunar orbit plane, sub-solar
marker, and sub-lunar marker. Use it when touching solar-system time, camera
controls, or moon rendering.

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --debug-view celestial-planes --output outputs/planet-celestial-planes.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 88 --planet-time-hours 18.13 --planet-camera-mode orbit --output outputs/planet-moon-occlusion-check.png
```

For moon occlusion, the important checks are visual rather than exact-clock
goldens: the depth-tested moon should disappear continuously behind the planet,
not pop based on center-only tests, and daytime moon visibility should wash out
through the lower atmosphere rather than remaining full contrast.

## Surface Field Checks

These captures pressure the current placeholder terrain and sea-level contracts:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-height --output outputs/planet-debug-terrain-height.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-slope --output outputs/planet-debug-terrain-slope.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view bathymetry --output outputs/planet-debug-bathymetry.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view shoreline --output outputs/planet-debug-shoreline.png
```
