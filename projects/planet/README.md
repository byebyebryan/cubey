# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version is intentionally small: it opens a window or headless capture
path, renders a cube-sphere debug planet surface, and provides the target
project boundary for future terrain, atmosphere, and ocean integration.

Run it with:

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --headless --frames 120 --output outputs/planet.png
./build/dev/projects/planet/planet --planet-radius-m 600000 --planet-camera-altitude-m 240000
./build/dev/projects/planet/planet --debug-view lod-level
```

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`, and
`screen-error`. The CPU LOD path is a first diagnostic implementation: it
subdivides cube-sphere patches by projected edge size and reports patch, LOD,
screen-error, and edge-length ranges in the UI.

This project should stay focused on planet-scale contracts first. Ocean scale
work remains in `projects/ocean` until the planet frame, LOD, and world-space
contracts are stable enough to port it cleanly.
