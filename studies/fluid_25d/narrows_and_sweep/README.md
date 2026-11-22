# Narrows and Sweep

An inlet pulse travels through 222 m of winding channel on a layered relief.
The water starts 2.00 m deep at the narrows. A longer release on the half-metre
finite-volume grid carries the crest through the lower bends. The film uses
three contours at 0.30, 0.60 and 0.90 m above the initial local surface, darker
tint as the surface rises, and translucency over the shallower bed.

The simulated crest rises about 0.94 m at the narrows and 0.70 m at the visible
outlet. The outlet stays connected across all 360 film frames. Bank measurements
on the half-metre grid of the visible reach recorded up to 0.084 m beyond the
nominal crest and zero sampled area deeper than 0.10 m.

## Reproduce the film

From the repository root, with a configured dev build:

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_atmosphere
python3 studies/fluid_25d/narrows_and_sweep/capture.py outputs/fluid/narrows-and-sweep
```

Use a fresh output directory. The capture needs Python 3.11+, a C++20 compiler
with GLM and nlohmann/json headers, Mesa EGL and desktop OpenGL development files
visible to `pkg-config`, and FFmpeg with libx264. To use the vendored JSON headers,
add the configured build's `_deps/nlohmann_json-src/include` to
`CPLUS_INCLUDE_PATH` as an absolute path.

The 12-second camera move arcs from 3° to 22° in yaw and lowers from 57° to
39° by the outlet crest at frame 275. It holds that pitch through recession,
keeping the whole reach in view. Solar time advances from
14:00 to 17:00. The Atmosphere app bakes clear sky at 14:00, 15:30 and
17:00 in `sky-only-no-ground-occlusion` mode. The relief pass blends adjacent
sky samples in linear light, reprojects them through the moving camera and
lights the relief with the shared solar direction and a restrained warm tint.
The terrain and water draw with Mesa llvmpipe. Use
`--atmosphere` for a different built binary, `--mesa-egl` for a different Mesa
GLVND manifest, or `--vulkan-icd /path/to/lvp_icd.json` to bake the sky on Mesa
lavapipe without using a hardware GPU.

The output contains the sky samples, solver fields and water ledger, full-grid
bank measurements, the 720p30 film and its outlet crest poster. `capture.json`
records generator arguments, file hashes and measured hydraulics. Playback
advances 5.4 simulated seconds per screen second. The display samples the
half-metre solver fields on a metre grid and uses a fixed 1.72× vertical scale.

See the [Fluid 2.5D guide](../../../projects/fluid/fluid_25d/README.md) for
interactive River V0 and the Vulkan shallow-water experiments.
