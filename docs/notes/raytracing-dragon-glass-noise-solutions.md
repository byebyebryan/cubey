# Established solutions for the glass dragon's solar noise

2026-11-16 research, following the
[matched speckle measurements](raytracing-dragon-glass-speckle.md).
This revises the proposed next pass in that note. Renderer commit `2e86702`
remains unchanged. No candidate noise treatment is implemented or selected.

The most useful first comparison is **a conventional HDR contribution clamp
and a standard still-image denoiser**, with the raw result retained. Footprint
filtering and layered reconstruction are the more substantial rendering work
to consider after judging that baseline. Deeper Fresnel splitting is a valid
general technique, but the replay below makes it a lower priority for these
observed glints. These priorities are Cubey-specific inferences from the
sources and current evidence, not published performance results for Cubey.

## What kind of paths need fixing

The previous investigation established that sunlight drives the bright tail.
This pass uses the existing private journal to replay two measured high-energy
samples at the original logical full-HD pixel coordinates and sample indices:

| Pixel / seed 19 / zero-based sample | Logged sharp events | Internal reflections | Surface IDs hit |
| --- | --- | --- | --- |
| (818,372), sample 873 | T R R R T | All total internal reflection, selection probability 1 | Certified glass only |
| (738,527), sample 1793 | T R R T | All total internal reflection, selection probability 1 | Certified glass only |

The first pixel is the unstable white-speck example; the second is the
highlight that repeated in the two earlier seeds. The replay RGB matches the
earlier sample log exactly for the first case and to one float32 ULP in one
channel for the second. Status flags are zero and synchronization validation
is clean. The end transmission probabilities are about .947 and .960.

This distinguishes their noise from rare low-probability *reflection-branch*
selection. These paths are primarily finding a tiny sun through a complicated
mapping from positions within the camera pixel. Extra splitting cannot split
a total-internal-reflection event into two positive-weight branches. This is
evidence for two important samples, not a census of every scene path.

The current renderer also makes the following distinctions:

- The first two dielectric events split Fresnel branches; deeper events sample
  a branch. Increasing that depth can help other path families at higher cost.
- The camera already jitters stratified samples within a pixel.
- The sun is evaluated using a direction and a binary solar-cone membership
  test. The propagated ray width/spread does not filter this solar evaluation.
- The existing optical cone uses a flat-interface approximation, with zero
  normal-field curvature in this route. Cone support for other shading is not
  equivalent to antialiasing the sun through varying smooth glass normals.

The [research evidence](../evidence/raytracing-dragon-glass-solution-research-2026-11/README.md)
records source locations, source identities and the diagnostic replay.

## Established approaches and their fit

### 1. Clamp HDR contributions before accumulation

This is a standard production compromise. NVIDIA's
[Omniverse path tracer](https://docs.omniverse.nvidia.com/materials-and-rendering/latest/rtx-renderer_pt.html#firefly-filtering)
documents enabled firefly filtering and separate glossy/diffuse ray-intensity
caps, explicitly acknowledging energy loss. Its threshold values are tied to
that renderer's units and should not be copied into Cubey.

The [RTXPT introductory shader](https://github.com/NVIDIA-RTX/RTXPT/blob/303b408ab81790a5051abd7573830b7a4112eb8e/Rtxpt/Shaders/IntroSample/IntroPathTracer.hlsl#L79)
caps each throughput-weighted radiance deposit and scales the RGB vector
uniformly to preserve hue. It applies the cap before adding the deposit to the
accumulated result. This is different from clipping the final display image.

**Fit:** a small, useful baseline for the demo. It intentionally biases bright
transport and can dim real highlights, so compare several thresholds against
the raw path. Preserve the clear-glass recipe; keep the threshold independent
of display exposure. A solar-only variant is an optional narrower experiment,
not the definition of the standard method.

### 2. Use a standard still-image denoiser for the existing HDR captures

[Open Image Denoise](https://www.openimagedenoise.org/documentation.html)
provides an HDR beauty filter with optional albedo/normal inputs. Its guidance
specifically discusses glass: feature buffers can follow secondary delta
paths and blend reflected/transmitted features with Fresnel weights. Plain
first-hit glass normals are not the only available guide choice.

**Fit:** a contained way to judge visible benefit before building a native
reconstruction pipeline. Start with a color-only offline baseline using our
retained HDR; better guides can follow if needed. Check loss of grooves, glints
and marble detail. Successful still denoising does not establish native-frame
cost or temporal stability, and no SDK has been installed/integrated here.

### 3. Filter the signal over the propagated pixel footprint

[Ray differentials](https://graphics.stanford.edu/papers/trd/) provide the
classical way to track how a camera pixel maps through reflection/refraction.
[Modern ray-cone work](https://research.nvidia.com/publication/2021-04_improved-shader-and-texture-level-detail-using-ray-cones)
provides practical GPU footprint/LOD machinery, chiefly for texture filtering.
Those references do not provide a complete curved-glass/sun filter to drop in.

**Fit:** the structural antialiasing direction for our narrow solar signal.
It needs a footprint-aware emitter evaluation and a suitable treatment of
normal variation, focusing and TIR boundaries. It should adapt to camera
resolution and zoom. Enlarging the actual sun globally, as in the earlier
private preview, changes the light and does not substitute for this filter.
This has greater implementation and validation scope than a clamp trial.

### 4. Reconstruct reflected and transmitted components with suitable guides

The current [RTXPT design](https://github.com/NVIDIA-RTX/RTXPT/blob/303b408ab81790a5051abd7573830b7a4112eb8e/Docs/Design.md),
labeled v1.9.0, uses a pure-specular pre-pass and path decomposition. It
generates separate guides for reflected/transmitted content; NRD can process
layers separately, while the DLSS-RR route combines selected material guides
and uses a dominant layer for depth/motion. This is a useful architecture
reference, not an integration we have tested.

[NRD's own guidance](https://github.com/NVIDIA-RTX/NRD) calls out transparency
limitations and the need for surface guides. The underlying
[path-decomposition research](https://cs.dartmouth.edu/~wjarosz/publications/zimmer15path.html)
explains why a pixel mixing reflection and transmission has conflicting
filtering/motion information. NVIDIA's earlier
[PSR article](https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/)
also limits its simple motion construction to static planar mirrors; arbitrary
curved glass is a harder case.

**Fit:** the substantial follow-up if moving-camera quality becomes the goal.
Our current accumulation resets on camera movement; it is not motion-vector
reprojection. A blind whole-image temporal average would risk trails and
smearing. Shared atmosphere/lighting can remain the radiance sources.

### 5. Spend rays on branch splitting or adaptive sampling

Tracing both specular reflection and transmission is the classical
[Whitted approach](https://pbr-book.org/3ed-2018/Introduction/pbrt_System_Overview).
[EARS](https://www.dfki.de/web/forschung/projekte-publikationen/publikation/12557)
studies allocating splitting/roulette effort using variance and tracing cost.
More branches trade work for reduced sampling variance rather than removing
the bright signal.

**Fit:** useful if a broader path survey finds important low-probability
Fresnel branches. It does not address the deterministic TIR events in the two
dominant examples above. Any trial must compare equal GPU time, not just equal
SPP, and retain transport-budget diagnostics. Adaptive pixel sampling can also
underestimate rare-event noise before enough bright paths have been seen.

### 6. Regularize paths or use specialized specular sampling

[PBRT's practical regularization](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
roughens scattering only after a non-specular event. Pure sharp-glass paths
therefore remain unchanged by that implementation. Applying a roughness floor
directly to our glass would be a different, biased material/transport choice.
The current dragon route also deliberately admits sharp glass only.

[Specular manifold sampling](https://rgl.epfl.ch/publications/Zeltner2020Specular)
constructs connections through specular chains with a solver instead of
depending entirely on chance hits. It is a larger research/integration topic;
the geometry, derivative model, multiple solutions and current normal-policy
fallback boundaries all matter. Ordinary NEE/MIS or a ReSTIR-DI switch does not
by itself supply such a connection through the glass.

**Fit:** neither is the first visible-finish pass. The former needs an explicit
appearance choice; the latter is a compelling dedicated sampling study if
that becomes the experiment's objective.

## Proposed next pass

This supersedes the earlier proposal to start with a custom solar-neighborhood
filter or reduced solar strength:

1. Establish conventional baselines: raw HDR, a hue-preserving HDR contribution
   clamp with a small threshold sweep, and an established offline denoiser.
   Do not change the mesh, sharp-glass material, atmosphere or sun size.
2. Compare 128/512-SPP stills across independent seeds at whole-scene size and
   in the head crop. Inspect glints, grooves and refracted marble for brightness
   loss or smearing. Record bias/appearance changes and processing cost; retain
   the unclamped, unfiltered path. The existing 8192-SPP images remain noisy
   comparisons rather than ground truth.
3. Select a demo default only after reviewing the actual look. If the remaining
   limitation is subpixel glint stability, scope a footprint-aware sun trial.
   If the goal expands to moving-camera reconstruction, plan the path-layer
   guides/history architecture explicitly before integrating NRD/DLSS-RR.

This is research and a recommendation. No threshold, denoiser, source split,
sampling policy, new default, commit, push or public-site update is included.
