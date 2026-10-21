# SDF

Boxes-only procedural SDF rendering project derived from projectR. The
[implementation plan](../../docs/notes/sdf-projectr-port-plan.md) defines the
checkpoints; the [source review](../../docs/notes/projectr-architecture-review.md)
and [development history](../../docs/notes/projectr-development-history.md)
preserve the reference and its experiments.

The first checkpoint establishes the complete CPU box recipe, explicit 48-byte
GPU upload layout, deterministic stick generation and ordered CSG fixtures.
The native GPU renderer is the next checkpoint; no visual/performance
qualification is implied by the recipe tests.

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_sdf_recipe_tests
ctest --preset dev -R '^sdf_recipe_tests$'
```
