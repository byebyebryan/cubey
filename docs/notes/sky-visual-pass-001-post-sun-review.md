# Sky Visual Pass 001 Post-Sun Review

Generated on 2026-06-22 under `outputs/sky-visual-pass-001-post-sun/`. The
PNGs and contact sheet are ignored by git; this note records the visual review
after adding the bounded atmosphere sun halo.

## Captures

```sh
mkdir -p outputs/sky-visual-pass-001-post-sun
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --time-of-day-mode solar --time-hours 17.8 --day-of-year 80 --latitude-degrees 30 --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001-post-sun/atmosphere-twilight-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset night --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001-post-sun/atmosphere-night-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --atmosphere-preset moonlit-night --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001-post-sun/atmosphere-moonlit-clean.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 2 --width 1280 --height 720 --debug-view sun-disk --sun-elevation 6 --sun-azimuth 90 --pause-time --no-reference-geometry --output outputs/sky-visual-pass-001-post-sun/atmosphere-sun-disk-debug.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --output outputs/sky-visual-pass-001-post-sun/planet-surface-sun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 4.75 --planet-camera-mode surface --planet-camera-surface-look antisun --planet-camera-surface-pitch-deg 22 --output outputs/sky-visual-pass-001-post-sun/planet-surface-antisun-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --output outputs/sky-visual-pass-001-post-sun/planet-orbit-limb.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 88 --planet-time-hours 18.13 --planet-camera-mode orbit --output outputs/sky-visual-pass-001-post-sun/planet-moon-occlusion.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 87.4 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/sky-visual-pass-001-post-sun/planet-day-moon-washout.png
```

## Observations

- Standalone `sun-disk` debug now reads as a small bright disk with a compact,
  warm halo instead of a single pinprick.
- Planet sun-facing dawn picks up the same bounded glow at the horizon without
  over-brightening the rest of the sky.
- Planet antisun dawn remains dark and subdued, so the halo is not turning the
  opposite horizon into a bright space-backdrop view.
- Twilight, night, moonlit night, orbit limb, and moon-occlusion captures remain
  consistent with the clean baseline.
- Day moon washout is effectively unchanged and should remain a later moon
  visibility/tuning slice.

## Decision

Accept the bounded atmosphere sun halo as the first visual tuning change. It
improves the unified atmosphere sun read in both standalone and planet paths
without broad sky exposure drift.
