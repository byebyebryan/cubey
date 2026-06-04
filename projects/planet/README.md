# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version is intentionally small: it opens a window or headless capture
path, renders a debug planet surface, and provides the target project boundary
for future cube-sphere LOD, terrain, atmosphere, and ocean integration.

Run it with:

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --headless --frames 120 --png outputs/planet.png
```

This project should stay focused on planet-scale contracts first. Ocean scale
work remains in `projects/ocean` until the planet frame, LOD, and world-space
contracts are stable enough to port it cleanly.
