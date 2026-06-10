# Planet Visual Capture Recipes

These recipes are meant for repeatable visual checks while the planet project is
still changing quickly. They are not golden-image tests yet; they are a compact
matrix for manually comparing LOD, atmosphere, celestial, and surface-view
behavior after renderer or camera changes.

All commands write under `outputs/`, which is intentionally ignored by git.

A small automated subset of this matrix is wired into CTest as headless PNG
smoke coverage. Those checks validate that key planet views produce non-empty
images with visible variation; this document remains the broader manual capture
matrix for visual comparison and tuning.

## Baseline Views

Use the mean solar clock controls to keep captures deterministic:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode orbit --output outputs/planet-orbit-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode orbit --output outputs/planet-orbit-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode orbit --output outputs/planet-orbit-night.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output outputs/planet-surface-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/planet-surface-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode surface --output outputs/planet-surface-night.png
```

Surface sky checks should use `--planet-camera-surface-look sun` or `antisun`.
The default surface heading is useful for terrain navigation, but it can miss
the sunrise/sunset lobe and produce misleading black-blue-white transitions.

## Sky Video Checks

Use these when changing the sky shader, celestial camera controls, exposure, or
night-sky atlas compositing:

```sh
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 23.0 --planet-time-speed-hours-per-second 0.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --planet-camera-orbit-spin-deg-per-sec 8.0 --planet-atmosphere-mode physical --output outputs/planet-sky-orbit-spin.mp4
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 4.35 --planet-time-speed-hours-per-second 0.03 --planet-camera-mode surface --planet-camera-altitude-m 1200 --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output outputs/planet-sky-surface-sun-twilight.mp4
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 4.35 --planet-time-speed-hours-per-second 0.03 --planet-camera-mode surface --planet-camera-altitude-m 1200 --planet-camera-surface-look antisun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output outputs/planet-sky-surface-antisun-twilight.mp4
```

## Atmosphere Comparison

The default sky backend is the unified foundation atmosphere fed by planet-owned
frame and celestial inputs. Keep `analytic` atmosphere-mode captures and
`sky-frame-legacy` backend captures around as comparison/debug fallbacks. The
legacy `shared-atmosphere` and `local` backend strings remain accepted only as
compatibility aliases.

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode analytic --output outputs/planet-atmo-analytic-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output outputs/planet-atmo-physical-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-atmosphere-mode physical --output outputs/planet-atmo-physical-backlit.png
```

Use these paired captures when comparing the unified atmosphere default against
the legacy sky-frame backend:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-sky-backend unified-atmosphere --output outputs/planet-sky-unified-surface-sun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-sky-backend sky-frame-legacy --output outputs/planet-sky-legacy-surface-sun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look antisun --planet-camera-surface-pitch-deg 22 --planet-sky-backend unified-atmosphere --output outputs/planet-sky-unified-surface-antisun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look antisun --planet-camera-surface-pitch-deg 22 --planet-sky-backend sky-frame-legacy --output outputs/planet-sky-legacy-surface-antisun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-sky-backend unified-atmosphere --output outputs/planet-sky-unified-orbit-limb.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-sky-backend sky-frame-legacy --output outputs/planet-sky-legacy-orbit-limb.png
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 23.0 --planet-time-speed-hours-per-second 0.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --planet-camera-orbit-spin-deg-per-sec 8.0 --planet-sky-backend unified-atmosphere --output outputs/planet-sky-unified-orbit-spin.mp4
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 23.0 --planet-time-speed-hours-per-second 0.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --planet-camera-orbit-spin-deg-per-sec 8.0 --planet-sky-backend sky-frame-legacy --output outputs/planet-sky-legacy-orbit-spin.mp4
```

## Surface Showcase

Use these when tuning procedural terrain or material response. They intentionally
exercise the default LOD profile, shoreline materials, mountain belts, and final
surface shading rather than only diagnostic colors.

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode orbit --output outputs/planet-showcase-orbit-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output outputs/planet-showcase-surface-sunrise.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --planet-sea-level-m 1500 --planet-shoreline-width-m 2800 --output outputs/planet-showcase-coastline.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --planet-terrain-height-scale-m 18000 --planet-terrain-mid-detail-strength 0.85 --planet-terrain-fine-detail-strength 0.18 --output outputs/planet-showcase-mountains.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-material --output outputs/planet-showcase-material-debug.png
```

For LOD comparisons, keep the camera/time fixed and vary only LOD parameters:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --planet-max-lod-level 7 --planet-lod-target-edge-px 8 --output outputs/planet-lod-previous-default.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --planet-max-lod-level 8 --planet-lod-target-edge-px 6 --output outputs/planet-lod-current-default.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --planet-max-lod-level 9 --planet-patch-resolution 128 --planet-lod-target-edge-px 5 --output outputs/planet-lod-stress.png
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
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode surface --output outputs/planet-moonlight-night.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/planet-day-moon-washout.png
```

For moon occlusion, the important checks are visual rather than exact-clock
goldens: the depth-tested moon should disappear continuously behind the planet,
not pop based on center-only tests. Daytime moon visibility should wash out by
losing contrast against the local sky instead of becoming transparent, and full
moon nights should show a small secondary moonlight response on terrain.

## Exposure And Camera Regressions

Use these after changing post exposure, solar time, camera transition, or orbit
controls. The goal is to catch sudden brightness jumps and pole-control flips,
not to match exact pixels.

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode orbit --output outputs/planet-exposure-orbit-lit.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --output outputs/planet-exposure-orbit-terminator.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode orbit --output outputs/planet-exposure-orbit-night.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/planet-exposure-day-moon.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --debug-view wireframe --output outputs/planet-camera-surface-wire.png
```

## Surface Field Checks

These captures pressure the current procedural terrain and sea-level contracts:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-height --output outputs/planet-debug-terrain-height.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-band-base --output outputs/planet-debug-terrain-band-base.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-band-relief --output outputs/planet-debug-terrain-band-relief.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-band-detail --output outputs/planet-debug-terrain-band-detail.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view terrain-slope --output outputs/planet-debug-terrain-slope.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view bathymetry --output outputs/planet-debug-bathymetry.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view shoreline --output outputs/planet-debug-shoreline.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view land-mask --output outputs/planet-debug-land-mask.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view moisture --output outputs/planet-debug-moisture.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view temperature --output outputs/planet-debug-temperature.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --debug-view roughness --output outputs/planet-debug-roughness.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-camera-mode surface --debug-view local-detail-horizon --output outputs/planet-debug-local-detail-horizon.png
```
