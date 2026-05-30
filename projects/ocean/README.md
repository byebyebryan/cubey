# Ocean

`ocean` is the active Cubey port of the wave-generation path from
[`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/). It
deliberately starts from the known-good reference core before any of Cubey's
older experimental macro waves, detail normal pass, foam history pass,
refraction, or seafloor shading are reintroduced.

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
./build/dev/projects/ocean/ocean --debug-view displacement --ocean-cascade 2
```

The GUI panel also includes cascade isolation, camera presets, a paused
single-frame step button, a portable wire overlay, and an LOD breakdown table
for checking clipmap coverage, patch counts, and triangle load while tuning the
mesh. Headless captures can use `--ocean-cascade all|0|1|2|3|4`,
`--ocean-wire-overlay`, and `--ocean-wire-opacity 0.0..1.0`.

Cascades are ordered from macro to detail for tuning: `0` is broad macro swell,
`1` is mid-scale macro chop, `2` is the primary reference crest, `3` is the
secondary reference wave, and `4` is fine normal/foam detail. The macro
cascades use low displacement, low normal contribution, and zero foam by default
so they can break up tiling without immediately creating broad whitecaps. The
current defaults are biased toward a stormier sea state with stronger C0/C1
macro displacement and more crest/foam energy on C2-C4.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
