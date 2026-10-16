# Terrain Water: integration and project ownership

The 2026-10-15 split separates application identity, not numerical methods.
No external solver is ported, no rendering implementation is copied, and no
producer is silently substituted. The [current behavior](current_reference.md)
and [setup](demo_setup_v1.md) remain separate from dated study evidence.

## Integration inventory

| Layer / consumer | Current integration | Boundary |
| --- | --- | --- |
| Shared Terrain asset layer | Source-bound sites, landscape catalogue and water-capture admission | Optional water is explicit; matching terrain alone does not imply a capture. |
| Shared surface renderer / capture runtime | Paired B-spline ground/water, materials, shadows, saved flow cues and environment | Display refinement is not finer simulated resolution or a new hydraulic state. |
| Terrain Water | Managed live SynxFlow, recorded presentation and explicit diagnostic builtin selection | Shows only the full-resolution simulation patch; owns worker lifecycle via its launcher. |
| Fluid 2.5D | Builtin virtual-pipes / finite-volume scenes and numerical oracles | Authored diagnostic results are not equivalent mountain-flow qualification. Old external/replay flags remain labelled compatibility entries. |
| Terrain demo | Named saved wet/dry landscapes and wider fixed-focus backdrop/LOD | No running solver needed. No-argument legacy startup remains unchanged; saved-landscape default promotion is separate. |
| glTF viewer | Published saved-water landscape using shared runtime/materials and fitted foreground/background depth | Opaque/masked foreground and clear sky; not general transparency/cloud/underwater composition. |
| Water 3D, Fire and Explosion | Existing ground-only TerrainBackdropRuntime options | New saved-water adoption is explicitly deferred, not accidentally claimed complete. |
| Ocean | Separate terrain-ocean field/bathymetry hooks | Not a consumer of inland captured runoff; no ocean/river coupling added. |

“Shared” means reusable contracts and components. It does not automatically add
water to every terrain view. Hiding a saved water surface retains wet-ground
shading; selecting a terrain-only preset loads no hydraulic state.

## Code and compatibility ownership

- `projects/terrain_water`: dedicated executable/config facade and canonical
  managed launcher/supervisor. Build target `cubey_project_terrain_water`.
- `projects/fluid/fluid_25d`: builtin facade and compatibility launcher.
  Build target `cubey_project_fluid_25d`; numerical tests remain under shared sim.
- `projects/terrain_water/tools`: maintained stdlib host utilities, input
  serialization, site catalogues and explicit case preparation. Normal startup
  imports no study modules. SynxFlow IO is imported only for explicit preparation.
- `studies/fluid_25d`: indexed historical notes and retained harnesses/tests.
- `projects/fluid/sim/fluid_25d/Fluid25DApp.cmake`: one shared application source
  list, compiled for each frontend's own shader directory, following the existing
  Fire/Explosion facade pattern. The implementation location is retained to
  avoid an unrelated numerical-code migration during the product split.
- `include/cubey`, `src/cubey`, `shaders/cubey/terrain`: shared asset, runtime
  and terrain/water rendering foundation, also used by Terrain and glTF.
- `projects/terrain_water/external_synxflow`: separately licensed worker/build
  source, locks and setup gates. The qualified ignored private binary retains its
  old absolute build path/RPATH; no artifact is relocated or requalified.

The old Python entry points forward to the canonical Terrain Water modules;
they do not fork orchestration or start a second worker. Established
`--fluid25d-*` flags, `cubey.fluid25d.*` schemas, helper import names, outputs
paths, namespaces and frozen recipes remain stable. Renaming all of them is
not required to establish distinct projects.

The launcher's historical `HERE` attribute still resolves to the retained helper
directory under `studies/fluid_25d/harness`, for retained validators/performance
tools only. `NATIVE_SUPPORT` names the worker source directory; catalogues live
under `tools`. `PROJECT_DIR` names the canonical frontend. None of these aliases
adds archived modules to the live launcher's import path.

The published media retains id `fluid-25d`, its original filenames and exact
capture/publication receipts. Current gallery metadata names its owner
`terrain_water` / **Terrain Water**. The builtin project has no newly promoted
film or equivalent-results claim.

## Builtin backup policy

The builtin frontend remains usable without external terrain, Python or CUDA.
Terrain Water accepts `--fluid25d-backend builtin` only as an explicit diagnostic
choice. It currently starts an authored builtin case, not a reconstructed copy
of a prepared SynxFlow mountain session. Existing capability guards remain in
force; unsupported generic controls are not advertised.

Recorded replay is the established no-CUDA viewing alternative for the featured
terrain result. Calling builtin a production-equivalent terrain backup requires
its own bounded scene/capability qualification later. Reset/restart is explicit;
no water-state transfer, hidden retry or silent fallback is implemented.

## Validation and promotion

The split must preserve builtin behavior, recorded pixels, shader artifacts,
source hashes, imported bed/depth/velocity and owned-process cleanup. The new
frontend adds producer/config tests and a synthetic GPU parity test without
external assets or imports. A separately invoked real-worker headless smoke
checks start, rainfall, pause and dry reset. Broader regression includes existing
Fluid, captured Terrain and glTF tests, excluding native window checks.

This is not a new performance qualification, solver study, native GUI acceptance,
default promotion or media publication. Water 3D and Pyro rollout remain out of scope.
