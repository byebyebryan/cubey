# Terrain Backdrop Foundation Evidence

This compact pack is the retained visual proof for the first shared Terrain V1
foundation milestone. Both captures use the canonical seed-0 Terrain Diffusion
raster, selected placement, a 200 m foreground height, filtered detail, terrain
shadows, and fixed atmosphere lighting. The manifest records the exact elevation
payload hash; generated source data remains outside Git.

## Terrain Review Consumer

![Terrain review app with the shared backdrop](terrain.png)

This frame verifies the isolated review host, continuous cached geometry,
foreground staging, atmosphere lighting, and terrain self-shadowing.

## glTF Proof Consumer

![glTF Viewer fallback object with the shared backdrop](gltf-viewer.png)

This frame verifies that the opt-in glTF path loads the same validated source,
composes terrain and foreground geometry in one depth domain, and retains the
atmosphere-backed PBR environment. The fallback object keeps the evidence pack
self-contained; no external glTF asset is required.

These images are review evidence, not golden-image tests. Normal CTest smokes
render the same paths at 64x64 with a small tracked analytical fixture and
enforce nonblank image statistics. `manifest.json` records the canonical source
hash, captured revision, image hashes, and validation output.

Regenerate the pack after building the two applications and `cubey_png_stats`
and generating the default terrain asset:

```sh
projects/terrain/capture_foundation_acceptance.sh
```
