# Ocean Ref

`ocean_ref` is an isolated Cubey port of the wave-generation path from
[`2Retr0/GodotOceanWaves`](https://github.com/2Retr0/GodotOceanWaves/). It is
kept separate from `projects/ocean` so the reference implementation can be
compared without mixing in Cubey's experimental macro waves, detail normal pass,
foam history pass, refraction, or seafloor shading.

GodotOceanWaves is MIT licensed; the required notice is kept in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Run a still capture:

```sh
./build/dev/projects/ocean_ref/ocean_ref --headless --frames 120 --width 1280 --height 720 --output /tmp/cubey-ocean-ref.png
```

Useful debug views:

```sh
./build/dev/projects/ocean_ref/ocean_ref --debug-view displacement
./build/dev/projects/ocean_ref/ocean_ref --debug-view normal
./build/dev/projects/ocean_ref/ocean_ref --debug-view foam
```

The default FFT map is `1024`. Smoke tests and fast local checks can use
`--ocean-ref-map-size 128`.
