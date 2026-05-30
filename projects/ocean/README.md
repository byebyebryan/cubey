# Ocean

`ocean` is the active Cubey port of the wave-generation path from
[`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/). It is
the active ocean renderer and deliberately starts from the known-good reference
core before any of Cubey's older experimental macro waves, detail normal pass,
foam history pass, refraction, or seafloor shading are reintroduced.

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
```

The GUI panel also includes a portable wire overlay and an LOD breakdown table
for checking clipmap coverage, patch counts, and triangle load while tuning the
mesh. Headless captures can use `--ocean-wire-overlay` and
`--ocean-wire-opacity 0.0..1.0`.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
