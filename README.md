# cubey

A personal GPU workbench for procedural graphics experiments and demos. Minimal C++ framework, maximum shader work.

The [original cubey](https://github.com/brynne8/cubey) (2015) covered fluid simulation, particles, marching cubes, and fractals in OpenGL/GPGPU. This is a ground-up rewrite using modern GPU APIs.

See [`docs/DESIGN.md`](docs/DESIGN.md) for the full plan.

## Current state

`webgpu` branch — hello triangle, native + web, both working.

## Building

### Native (Linux)

Requires **Clang** (GCC 15 has a compatibility issue with Dawn — see [`docs/webgpu-notes.md`](docs/webgpu-notes.md)).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j$(nproc)
./build/cubey
```

First configure pulls Dawn, GLFW, and glfw3webgpu via FetchContent — takes a while.

### Web (Emscripten)

Requires [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (`emsdk` or distro package).

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j$(nproc)
python3 -m http.server 8002 --directory build-web
# open http://localhost:8002/cubey.html
```

Chrome on Linux may need `--ignore-gpu-blocklist --enable-unsafe-webgpu` if your GPU is new/unknown to Chrome. Firefox works out of the box.

## Stack

| Layer | Choice |
|-------|--------|
| Language | C++20 |
| GPU API | WebGPU (Dawn native / emdawnwebgpu web) |
| Windowing | GLFW 3.4 |
| Build | CMake + Ninja |
