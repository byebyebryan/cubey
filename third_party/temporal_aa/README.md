# Temporal resolve provenance

`shaders/cubey/temporal_aa/resolve.comp` adapts the explicitly MIT-licensed
[public Donut TAA shader](https://github.com/NVIDIA-RTX/Donut/blob/52efbe37d7f550ea1c5c7ee02b75404ec7c72c09/shaders/passes/taa_cs.hlsl),
pinned to commit `52efbe37d7f550ea1c5c7ee02b75404ec7c72c09`.
The copyright and MIT terms are retained in `LICENSE`.

The port selects native-resolution single-sample PQ feedback, 3x3 variance
clipping, longest neighborhood motion, fixed temporal weight and Catmull-Rom
history filtering. Upscaling and the history-clamp relaxation mask are excluded.
PQ range is fixed at 10000. Explicit zero out-of-bounds loads reproduce HLSL
texture-load behavior. Cubey adds finite radiance/motion guards and a one-pixel
viewport inset. Uniforms, history ownership and motion reconstruction are Cubey code.
No DLSS library, NVRHI or DXC is required to build.

The DLSS v310.9.16 Linux sample is execution/reference evidence only. Its bundled
shader has no MIT notice and its companion C++ and headers carry proprietary
notices; the earlier planning claim that this bundle was the MIT donor was wrong.
No bundled C++ or constant header is vendored. The shared algorithm subset is
sourced from the public MIT shader above.
