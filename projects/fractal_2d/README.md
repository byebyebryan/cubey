# Fractal 2D

An interactive fullscreen fractal viewer with project-owned navigation and
headless capture. This is a rendering example, not a terrain/fluid project.

```sh
rtk proxy cmake --build --preset release --target cubey_project_fractal_2d -j 6
rtk proxy build/release/projects/fractal_2d/fractal_2d
```

Add `--headless --frames 1 --output outputs/fractal-FRESH.png` for a capture.
View/navigation and image smoke contracts are registered in CTest. Generated
captures belong under ignored `outputs/`, not the application source folder.
