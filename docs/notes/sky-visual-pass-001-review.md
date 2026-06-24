# Sky Visual Pass 001 Review

Generated on 2026-06-22 under `outputs/sky-visual-pass-001/`. The PNGs and
contact sheet are ignored by git; this note records the review baseline before
sun halo tuning.

## Captures

```sh
mkdir -p outputs/sky-visual-pass-001
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --time-of-day-mode solar --time-hours 17.8 --day-of-year 80 --latitude-degrees 30 --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001/atmosphere-twilight-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset night --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001/atmosphere-night-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001/atmosphere-moonlit-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view sun-disk --sun-elevation 6 --sun-azimuth 90 --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001/atmosphere-sun-disk-debug.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --output outputs/sky-visual-pass-001/planet-surface-sun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look antisun --planet-camera-surface-pitch-deg 22 --output outputs/sky-visual-pass-001/planet-surface-antisun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --output outputs/sky-visual-pass-001/planet-orbit-limb.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 88 --planet-time-hours 18.13 --planet-camera-mode orbit --output outputs/sky-visual-pass-001/planet-moon-occlusion.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/sky-visual-pass-001/planet-day-moon-washout.png
```

## Observations

- `--no-reference-geometry` produces clean standalone atmosphere review frames
  without the red ground-reference marker.
- Standalone twilight has a broad warm horizon, but the dedicated sun-disk debug
  view shows only a tiny disk with no halo.
- Planet sun-facing dawn frames the sun correctly, but the disk reads as a small
  point over the horizon band. This is the immediate tuning target.
- Planet antisun dawn remains dark and subdued; it should not become a bright
  space-backdrop view during sun halo tuning.
- Orbit limb and moon-occlusion captures frame the integrated atmosphere and
  moon-body ownership checks well enough for before/after comparison.
- Day moon washout remains visually weak by eye and should stay a later moon
  validation/tuning slice.

## Next

Add a bounded halo around the existing atmosphere sun disk, then regenerate this
same matrix under `outputs/sky-visual-pass-001-post-sun/`.
