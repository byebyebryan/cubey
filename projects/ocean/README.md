# Ocean

`ocean` is the active Cubey port of the wave-generation path from
[`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/). It
deliberately starts from the known-good reference core before any of Cubey's
older experimental macro waves, detail normal pass, foam history pass,
refraction, or seafloor shading are reintroduced.

GodotOceanWaves is MIT licensed; the required notice is kept in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Known-good fallback before the LOD data/domain architecture work:
`ocean-known-good-material-v1` (`400d45c`).

Run a still capture:

```sh
./build/dev/projects/ocean/ocean --headless --frames 120 --width 1280 --height 720 --output /tmp/cubey-ocean.png
```

Useful debug views:

```sh
./build/dev/projects/ocean/ocean --debug-view displacement
./build/dev/projects/ocean/ocean --debug-view normal
./build/dev/projects/ocean/ocean --debug-view foam
./build/dev/projects/ocean/ocean --debug-view foam-source
./build/dev/projects/ocean/ocean --debug-view lod
./build/dev/projects/ocean/ocean --debug-view lod --ocean-wire-overlay
./build/dev/projects/ocean/ocean --debug-view displacement --ocean-cascade 2
./build/dev/projects/ocean/ocean --no-ocean-spectral-domains
```

The GUI panel also includes cascade isolation, camera presets including a wide
repeat-inspection camera, a paused single-frame step button, a portable wire
overlay, and an LOD breakdown table for checking clipmap coverage, patch counts,
and triangle load while tuning the mesh. Headless captures can use
`--ocean-cascade all|0|1|2|3|4`,
`--ocean-wire-overlay`, `--ocean-wire-opacity 0.0..1.0`,
`--ocean-spectral-domains`, and `--no-ocean-spectral-domains`.

Cascades are ordered from macro to detail for tuning: `0` is broad macro swell,
`1` is mid-scale macro chop, `2` is the primary reference crest, `3` is the
secondary reference wave, and `4` is fine normal/foam detail. The macro
cascades use low displacement, low normal contribution, and low foam by default
so they can break up tiling without creating broad whitecaps alone. The current
defaults are biased toward a stormier sea state with foam driven mostly by C2
and C3, with C4 adding fine breakup and C0/C1 kept from creating broad cloudy
white sheets. The anti-repeat control keeps a conservative C0/C1 geometry blend
and adds distance-gated C1-C4 normal/foam anti-tiling for far-field whitecaps.
Spectral domains are enabled by default to remove tile-sized waves from each
cascade while preserving overlap between source bands; foam is stored separately
from normal data as persistent history, current breaking source, determinant,
and compression channels so whitecap coverage can be sharpened without damaging
the normals. This is still not a localized wind or weather simulation.

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-map-size 128`.
