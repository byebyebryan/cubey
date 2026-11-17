# Glass dragon: established solutions for moving-camera noise

Research on 2026-11-16, after the user reviewed the
[native orbit/stop recording](http://starship:8787/reports/15e94fe01be8bf061a4e4bb42c815831b65a758d1f717e608e8dfb1d2394b169/).
This is a proposed direction, not an implemented reconstruction pipeline.
The later user-selected simpler-demo direction supersedes its NRD-first
recommendation; use the [demo transport plan](raytracing-dragon-demo-transport-plan.md)
for the next proposed pass. The reconstruction survey remains a reference if
stochastic motion reconstruction is explicitly reopened.
The [evidence index](../evidence/raytracing-dragon-motion-research-2026-11/README.md)
includes the source manifest, code audit and validation for this pass.

## Diagnosis and standard solution

The recording uses the accepted sharp-glass model, smooth normals, cap 100,
shared atmosphere and a 256-sample target. All 202 native frames receiving
drag input end at one sample. The renderer clears its full HDR history when
the camera changes, so these frames receive no reconstructed temporal history.
After stopping, the fixed-view image accumulates normally.

This is the expected low-sample problem addressed by **spatiotemporal
reconstruction**. A renderer reprojects previous results into the current
view, rejects history at newly exposed or incompatible surfaces, and combines
temporal reuse with spatial filtering. The foundational
[SVGF paper](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/)
uses temporal accumulation, luminance variance and a hierarchical wavelet
filter to reconstruct a sequence from one path per pixel. Simply retaining
the old image at unchanged screen coordinates would smear motion.

## Available approaches

### Production real-time denoisers

[NRD 4.18.0](https://github.com/NVIDIA-RTX/NRD/blob/90dea8bd91ec6b58d0f90cb6adf53c7da613b82c/README.md)
provides REBLUR and RELAX with motion, normal/roughness, view depth and
lighting/hit-distance inputs. Linux build scripts and Vulkan/SPIR-V integration
are documented. It is a credible library candidate for Cubey; it has not been
built or integrated here. Its core denoisers target opaque surfaces, so the
application must handle the glass signal appropriately.

[DLSS Ray Reconstruction](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md)
is another established route. Its guide requires noisy color, reflectance,
normals/roughness, depth and motion, including specular tracking information.
It likewise needs renderer-produced inputs. This pass does not qualify an SDK
deployment path for Cubey's Linux host.

### Glass-aware path layers

A point on the dragon's front surface, the marble seen through it and a
reflected feature do not share one apparent motion. Aligning the glass surface
alone does not align its contents. First-hit guides can consequently drag or
blur the refracted image.

[RTXPT 1.9.0](https://github.com/NVIDIA-RTX/RTXPT/blob/303b408ab81790a5051abd7573830b7a4112eb8e/Docs/Design.md)
traces a pure-specular pre-pass and separates primary, reflected and transmitted
layers. Its NRD route filters layers separately; its DLSS-RR route blends some
guides and selects a dominant layer for depth/motion. This is an architecture
reference, not a promise of exact curved-glass motion. The older
[PSR article](https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/)
explicitly limits its simple mirror-motion construction to static flat mirrors.

### A dedicated transparent pass plus TAA

The current
[NRD sample transparent pass](https://github.com/NVIDIA-RTX/NRD-Sample/blob/35f5432ccbec3d234230b43a7216b40e65a58bdc/Shaders/TraceTransparent.cs.hlsl)
is a different practical reference: trace reflection/refraction, reuse denoised
opaque radiance where valid, use cache/lighting fallbacks, then apply TAA or
upscaling. It uses a curved-glass motion heuristic. The supporting glass tracer
has a nonconvex-extinction TODO, so its transport code must not replace Cubey's
qualified medium tracking. Its reconstruction ideas can be studied separately.
Radiance reuse also does not directly eliminate our delta-path solar hits.

### Useful supporting tools, with narrower scope

The [OIDN RT documentation](https://www.openimagedenoise.org/documentation.html#rt)
explicitly says this filter is not temporally stable. Per-frame OIDN is a
useful comparison, but our earlier still results do not establish motion quality.

[Spatiotemporal blue noise](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/)
can improve low-sample noise distribution. Its authors document limitations
under motion and high-dimensional path tracing. Better sampling and additional
samples per frame are supporting controls, not substitutes for reconstruction.

## Fit to Cubey and proposed first pass

My recommendation is to investigate **NRD as the first library candidate**, with
glass-specific guide handling treated as an explicit feasibility gate. Avoid
writing a general denoiser from scratch or assuming a final-color SDK call will
solve the dragon. Choose layered reconstruction versus a dedicated glass pass
from that guide/quality proof, rather than assuming the two are interchangeable.

The current gaps are a single fixed-view history, no previous-view reprojection
or production motion/depth/normal/path-layer buffers, and one combined beauty
signal. Diagnostic geometry records are not a persistent denoiser guide system.
Sampling is indexed by the completed history count, so a reconstruction mode
also needs a sample/frame sequence independent of history age. The static
scene simplifies opaque motion; it does not remove glass's view dependence.

Proposed acceptance sequence:

1. Prove the pinned library's Linux/Vulkan build and required buffer contract.
   Define guide policies for reflected/transmitted hits, environment misses,
   TIR/path changes and disocclusions before integration. An opaque-only result
   does not count as solving the glass dragon.
2. Add an opt-in reconstruction prototype after HDR tracing and before the
   existing display transform. Retain the raw accumulator and ordinary
   reference sequences. Reuse valid history under camera motion; hard-reset
   incompatible history on cuts, resize and material/lighting changes.
3. Record the same orbit/stop trajectory in raw and reconstructed modes at
   normal speed. Judge moving grain, head detail, refracted marble, silhouettes,
   trails, sticking and response after stopping. Measure GPU/resource cost
   separately from the virtual-display recording. Reject a smoother result if
   it merely exchanges grain for conspicuous ghosting or lost structure.

Keep the current model, material, cap and atmosphere fixed. This proposal does
not require adding GI. No SDK install, renderer changes, commit, push or site
publication is part of this research pass.
