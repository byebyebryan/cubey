# Procedural Shader Parity

Date: 2026-06-18

This note records the first parity pass between CPU-side `cubey::procedural`
helpers and shared GLSL helpers under `shaders/cubey/procedural`.

The goal is not to migrate more project formulas yet. The goal is to make it
clear which helpers are safe cross-language contracts and which helpers are only
shared shader vocabulary.

## Tested Parity Contracts

These helpers have CPU tests that mirror the GLSL formulas:

- scalar shaping: saturate, remap, smoothstep01, and smootherstep01;
- integer hash: `hash_u32(uint)`;
- GLSL masked hash-to-unit: `cubey_proc_hash01_u32`;
- 3D lattice hash: `cubey_proc_hash_u32_3d`;
- 3D signed value noise: `cubey_proc_value_noise_3d`;
- 3D FBM: `cubey_proc_fbm_3d`.

The CPU side already has legacy 3D value noise and FBM formulas that match the
GLSL 3D helpers. The CPU side now also exposes small scalar parity helpers and
the GLSL-specific masked hash-to-unit conversion.

## Current Ownership Buckets

Treat active procedural shader code as four ownership classes before moving it
into a shared include:

- Shared parity contracts: `shaders/cubey/procedural/operators.glsl`,
  `random.glsl`, and the exact 3D value-noise/FBM family in `noise.glsl`.
- Shared shader vocabulary: PCG and sin-dot hash/value helpers used by multiple
  shaders, but not yet promised as CPU contracts.
- Domain-owned formulas: cloud volume/weather shaping, ocean spectrum/foam
  breakup, atmosphere star and moon recipes, fluid turbulence, and project
  diagnostics that encode a renderer or simulation decision.
- Reference-owned formulas: `cloud_ref`, `cloud_ref_2`, `clouds_legacy`, and
  other kept-close-to-source snapshots.

Only the first bucket can be migrated mechanically. The second bucket needs a
golden-value pass before becoming CPU/GPU API. The third bucket should stay
project-owned unless a focused feature proves a reusable domain abstraction. The
fourth bucket should not be deduplicated while it is still useful as reference
evidence.

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

## FastNoiseLite GLSL Status

`shaders/cubey/procedural/fastnoise_lite.glsl` is the shared shader include for
the upstream FastNoiseLite GLSL port. It exists so future shader work can depend
on one Cubey include path instead of project-local vendoring. The current
coverage is compile-smoke only: a test shader includes FastNoiseLite and the
shared procedural headers, instantiates `fnl_state`, and calls `fnlGetNoise3D`.

That is not yet a numeric parity contract. FastNoiseLite C++ and GLSL should be
promoted to a CPU/GPU contract only after a dedicated dispatch/readback fixture
compares fixed samples against `cubey::procedural` on a real Vulkan device.

## Gate For Future Migrations

New GLSL migrations should start by asking whether the helper belongs to one of
three buckets:

- exact CPU/GLSL parity contract;
- intentionally distinct legacy or shader contract;
- shader-only visual formula.

Active cloud, ocean, atmosphere, and fluid shader formulas should not migrate to
FastNoiseLite GLSL until they have parity coverage or a specific visual-retuning
commit. GPU-executed shader golden tests remain a separate follow-up because
they need real-device dispatch and readback plumbing, not just shared include
coverage.
