# Ocean

`ocean` is the active Cubey port of the wave-generation path from
[`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/). It
deliberately starts from the known-good reference core, then experiments in the
active project with five overlapping wavelength cascades and a shared sea-state wind model
before any of Cubey's older experimental macro waves, detail normal pass, foam
history pass, refraction, or seafloor shading are reintroduced.

GodotOceanWaves is MIT licensed; the required notice is kept in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Run a still capture:

```sh
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output /tmp/cubey-ocean.png
```

Useful debug views:

```sh
./build/dev/projects/ocean/ocean --debug-view displacement
./build/dev/projects/ocean/ocean --debug-view normal
./build/dev/projects/ocean/ocean --debug-view foam
./build/dev/projects/ocean/ocean --debug-view lod
./build/dev/projects/ocean/ocean --debug-view lod --ocean-wire-overlay
./build/dev/projects/ocean/ocean --debug-view displacement --ocean-cascade 0
```

The GUI panel also includes cascade isolation, camera presets, a paused
single-frame step button, a sea-state section, a portable wire overlay, and an
LOD breakdown table for checking clipmap coverage, patch counts, and triangle
load while tuning the mesh. Headless captures can use
`--ocean-cascade all|0|1|2|3|4`,
`--ocean-wire-overlay`, and `--ocean-wire-opacity 0.0..1.0`.

The active default adds restrained macro `224..768 m` and long `88..320 m`
windows in front of the reference-like `88 m`, `57 m`, and `16 m` cascades.
The windows intentionally overlap so the reference-style crest interference
survives while zoomed-out tile repetition is reduced.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
