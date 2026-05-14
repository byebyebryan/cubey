# Animation And Deformation

Cubey treats glTF animation and vertex deformation as a rendering-engine
foundation feature, not as demo-local glue. The contract follows glTF 2.0
terminology and keeps the same broad separation used by engines such as
Filament: CPU asset data, scene/runtime playback state, stable render resource
handles, and GPU-side deformation buffers where vertex work becomes expensive.

## Shape

- Rigid animation is sampled on CPU and written into `TransformManager3D` as
  local TRS edits.
- Morph animation samples weights on CPU, but applies target deltas on GPU.
- Skinning samples node transforms on CPU and builds joint palettes from scene
  world transforms plus inverse bind matrices.
- Skin-only, morph-only, and morph-plus-skin meshes use one compute deformation
  path. Morphing runs first in mesh space; skinning then writes into a per-frame
  output vertex buffer.

The output of GPU deformation is an ordinary render mesh stream. Shadow,
depth, and PBR passes should not need to know whether a mesh was static,
morphed, skinned, or both; they resolve the correct frame-slot mesh and bind it
through the normal draw path.

## Ownership

`cubey::asset` owns decoded glTF animation, skin, morph target, and vertex
influence data. It does not create scene entities, Vulkan buffers, descriptors,
or playback state.

`cubey::animation` owns pure CPU sampling and playback contracts: clip time,
looping, interpolation, sampled node TRS, sampled morph weights, and joint
palette math.

`cubey::engine` owns the asset-to-scene bridge. It maps glTF nodes to entities,
creates renderables, applies sampled rigid transforms, and owns imported
deformation resources for an active scene import.

`cubey::render` owns reusable GPU-facing deformation primitives: storage-capable
vertex buffers, per-frame output buffers, render-graph synchronization, compute
pipelines, and mesh resolution by `FrameSlot`.

## V1 Limits

- One active animation clip per imported asset in the viewer; select it with
  `--animation-index`, scale playback with `--animation-speed`, or hold the
  first sampled pose with `--pause-animation`.
- No blending, retargeting, state machines, or additive clips yet.
- Core glTF `translation`, `rotation`, `scale`, and `weights` channels only.
- `STEP`, `LINEAR`, and `CUBICSPLINE` interpolation are part of the contract.
- `JOINTS_0` and `WEIGHTS_0` are supported; additional influence sets are
  rejected until the skinning contract needs them.
- Sparse accessors are rejected until the loader has a deliberate expansion
  path.
- Animated/deformed bounds are conservative CPU-side bounds in V1; no GPU
  bounds reduction or readback is planned for this slice.

## Validation Assets

Khronos glTF Sample Assets are the primary external validation set:

- rigid: `AnimatedTriangle`, `AnimatedCube`, `BoxAnimated`,
  `InterpolationTest`;
- morph: `SimpleMorph`, `AnimatedMorphCube`, `MorphPrimitivesTest`,
  `MorphStressTest`;
- skin: `SimpleSkin`, `RiggedSimple`;
- later stress: `CesiumMan`, `Fox`, `RecursiveSkeletons`.

The sample asset checkout stays optional through CMake. Tests and smoke runs
should use it when configured, but the repository should not vendor large
sample assets by default.
