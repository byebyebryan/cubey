# Saved-water landscape capture history

This page retains the former glTF/Terrain composite recipe. It is historical
presentation evidence, not the current glTF showcase. The current viewer
presentation is feature-led; the composite reused prepared Terrain and saved
water fields and launched no hydraulic solver. The [study record](../../../studies/gltf_viewer/water_landscape_v1.md)
documents its scope and depth choices.

The daylight inspection command uses an existing prepared Terrain cache and a
local asset. It does not download or generate terrain, install SynxFlow, or run
a solver:

```sh
rtk proxy build/release/projects/gltf_viewer/gltf_viewer --terrain-landscape mountain-local --no-clouds --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --time-of-day-mode solar --time-hours 10 --time-speed-hours-per-second 0 --capture-camera-distance-scale .70 --capture-camera-yaw 94.538 --capture-camera-pitch -6.8755 --exposure 0
```

The native 1080p source capture command is retained exactly as recorded:

```sh
rtk proxy build/release/projects/gltf_viewer/gltf_viewer --headless --capture video --frames 480 --fps 60 --width 1920 --height 1080 --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --terrain-landscape mountain-local --terrain-landscape-depth fitted --no-clouds --time-of-day-mode solar --time-hours 14 --time-speed-hours-per-second .6875 --capture-camera-distance-scale .70 --capture-camera-yaw 0 --capture-camera-pitch -6.8755 --capture-video-orbit-degrees 45 --star-intensity 0 --ibl-intensity 1 --require-validation --terrain-water-audit --output outputs/showcase/gltf-landscape-A-solar-source.mp4
```

The [showcase archive package](../../../docs/media/showcase/README.md) and
[media manifest](../../../docs/media/showcase/manifest.json) retain the film,
poster, native master, exact recipe, publication receipt and provenance. The
saved-water record does not demonstrate lake formation, calibrated weather,
permanent water or live fluid simulation. Its media-only CC-BY-NC-4.0 license
does not change the Cubey source license. Previous dry-terrain and fixed-morning
cuts remain identified by their original records in the archive package.
