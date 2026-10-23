# PBR material workshop

Implementation in progress under the [reviewed plan](../../docs/notes/pbr-material-demo-plan.md).
The first checkpoint is an ordinary mesh sphere gallery using the shared
`ForwardPbrRenderer3D`, with native headless capture and windowed orbit control.
The shared catalog lives in `assets/materials`; SDF evaluation has been promoted
mechanically into `cubey::procedural` with compatibility aliases.

Build: `cmake --build build/release --target cubey_project_pbr_materials`.
Run: `build/release/projects/pbr_materials/pbr_materials`.
