# PBR Furnace

White-furnace checks for shared PBR, with explicit IOR, specular, clearcoat,
anisotropy, iridescence and transmission conformance cases. This is a diagnostic
project, not a scenic showcase.

```sh
rtk proxy cmake --build --preset release --target cubey_project_pbr_furnace -j 6
rtk proxy build/release/projects/pbr_furnace/pbr_furnace
```

Add `--headless --frames 1 --output outputs/furnace-FRESH.png` for a capture.
`--conformance-case ior` selects one retained fixture. Config, scene and image
checks are registered in CTest. Keep generated results under ignored `outputs/`.
