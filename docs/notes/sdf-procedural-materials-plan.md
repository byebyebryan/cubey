# Procedural stone material audition

Status: implementation in progress, 2026-10-22.

Compare carved limestone and honed marble with the plain mirrored guardian.
Keep geometry, its 65 atomic operations, field resolution, hard self-shadows and
shared atmosphere unchanged. This is surface appearance, without image maps,
UVs, geometric displacement, specular environment lighting or a material graph.

## Contract

- Optional per-material procedural parameters: type, deterministic seed, fixed
  model-space scale/offset, secondary color, roughness variation, fine grain
  amplitude/scale and marble vein controls. Preserve them on save/reload and in
  the appearance fingerprint; geometry identity and field cache remain stable.
- A common model-space volume spans all segment leaves and both mirrored sides.
  Coordinates never depend on revealed bounds or edit index. Cuts expose the
  same volume. Geometry symmetry does not mirror grain.
- Evaluate material patterns once per material at a surface query, rather than
  once per primitive or at distance-traversal samples. Preserve existing hard,
  cut and smooth material ownership/blending. Qualify a one-material shading
  shortcut separately from procedural cost.
- Add analytic fine-grain height gradients for bump shading, with pixel-footprint
  filtering. Trace shadows/AO and offset rays with geometric normals; keep bump
  normals in the geometric hemisphere. Do not change silhouettes or ray depths.
- Keep plain/pattern/bump switches and shaded, base-color, roughness and normal
  inspection views. Material edits must not rebuild distance bricks.

## Checkpoints

1. Implement parser, persistence, identity, CPU/GPU procedural evaluation,
   filtering and normal separation, with focused material/parity tests and
   plain/multimaterial regression images. Commit the foundation.
2. Author restrained limestone and honed-marble presets on the same geometry.
   Review matching hero, front, detail, rear and raking-light views; iterate on
   actual native images. Preserve the plain guardian.
3. Qualify all new material/history states, cache/persistence, deterministic
   seeds and cut continuity. Capture material turntables and a marble carving
   film. Measure shortcut/plain, pattern-only and bump GPU costs separately at
   1080p, with fixed settings and no competing owned GPU work.
4. Publish a compact comparison report with controls, films, timings and honest
   limitations; inspect available browser interactions and native frames. Update
   docs and commit milestones locally. Do not push.

References carried from planning: PBRT's
[solid textures](https://www.pbr-book.org/3ed-2018/Texture/Solid_and_Procedural_Texturing),
[filtered noise and marble](https://www.pbr-book.org/3ed-2018/Texture/Noise), and
[bump mapping](https://www.pbr-book.org/4ed/Textures_and_Materials/Material_Interface_and_Implementations#BumpMapping).
