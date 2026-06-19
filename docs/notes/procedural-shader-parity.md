# Procedural Shader Parity

Date: 2026-06-18

This note scopes the first parity pass between CPU-side `cubey::procedural`
helpers and shared GLSL helpers under `shaders/cubey/procedural`.

The goal is not to migrate more project formulas yet. The goal is to make it
clear which helpers are safe cross-language contracts and which helpers are only
shared shader vocabulary.

## Parity Targets

These helpers should have exact CPU/GLSL parity tests:

- scalar shaping: saturate, remap, smoothstep01, and smootherstep01;
- integer hash: `hash_u32(uint)`;
- GLSL masked hash-to-unit: `cubey_proc_hash01_u32`;
- 3D lattice hash: `cubey_proc_hash_u32_3d`;
- 3D signed value noise: `cubey_proc_value_noise_3d`;
- 3D FBM: `cubey_proc_fbm_3d`.

The CPU side already has legacy 3D value noise and FBM formulas that match the
GLSL 3D helpers. The missing CPU surface is small scalar naming around
`smoothstep01`, `remap`, and the GLSL-specific masked hash-to-unit conversion.

## Intentionally Distinct Helpers

`cubey::procedural::hash_to_unit` and GLSL `cubey_proc_hash01_u32` are not the
same contract:

- CPU `hash_to_unit` uses the high 24 bits of the hashed integer and divides by
  `2^24 - 1`;
- GLSL `cubey_proc_hash01_u32` uses the low masked 24 bits and divides by
  `2^24`.

Both should remain available because existing CPU 3D noise relies on the
high-bit helper, while fluid and particle shaders already use the masked shader
helper. The parity pass should add a CPU helper for the GLSL behavior instead
of changing the existing CPU helper.

## Shader-Only Visual Formulas

These helpers should stay documented as shader-only visual formulas for now:

- `cubey_proc_hash_pcg_2d`;
- `cubey_proc_hash_pcg_3d`;
- `cubey_proc_hash_sindot_2d`;
- `cubey_proc_value_noise_sindot_2d`;
- `cubey_proc_value_noise_pcg_2d`.

They are shared include helpers because several shaders use them, but they do
not yet define a CPU procedural contract. Promote them only after a focused
golden-value or visual migration pass proves the need.

## Gate For Future Migrations

New GLSL migrations should start by asking whether the helper belongs to one of
three buckets:

- exact CPU/GLSL parity contract;
- intentionally distinct legacy or shader contract;
- shader-only visual formula.

FastNoiseLite GLSL parity, GPU-executed shader golden tests, and additional
shader formula migrations are follow-up work after this first CPU-mirrored
parity pass.
