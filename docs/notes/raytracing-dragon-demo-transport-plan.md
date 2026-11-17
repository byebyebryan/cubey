# Dragon RT demo: a simpler transport plan

Implementation follow-up: [opt-in prototype results and forward comparison](raytracing-dragon-demo-results.md).
The proposal below is retained as the research/acceptance record; it is not a
claim that the sampled defaults have been replaced.

2026-11-16 research and proposed first pass. The user selected a simpler demo
direction after reviewing the native movement noise: responsive presentation
takes priority over optical completeness. This supersedes the proposed NRD-first
implementation in [the motion reconstruction survey](raytracing-dragon-motion-research.md).
No renderer changes are included in this research pass.
The [evidence index](../evidence/raytracing-dragon-demo-research-2026-11/README.md)
records the source references, code audit and validation for this proposal.

## Recommendation

Prototype one **deterministic glass / cached-lighting mode** in the existing RT
application. Hardware rays still find the dragon's real reflected/refracted
geometry and the marble base. Opaque hits use the existing shared PBR lighting;
escaped rays use the existing visual sky. Primary visibility stays ray traced;
this prototype does not require a new hybrid primary-visibility architecture.

The target is a pleasing moving demo that demonstrates what scene rays add over
a practical forward approximation. Add a matched forward comparison using the
existing shared renderer, subject to the small integration proof below. Keep
the current sampled mode as an internal diagnostic comparison, rather than
presenting sampled versus deterministic RT as an RT-on/off demonstration.
Do not add NRD, DLSS, OIDN, motion-vector
layers, GI, clouds, a new mesh, or a new normal/footprint solver in this pass.

## Forward comparison: purpose and bounded scope

The user proposed this comparison after the initial transport plan. It gives
the demo a clearer question: **which visible scene interactions do actual
reflection/refraction rays add, and at what frame cost?** This is a proposed
extension to the plan, not an implemented feature or an adopted default.

Use the same certified dragon, convex marble slab, smooth normals, camera,
sun/atmosphere assets, material inputs, resolution, exposure and display curve.
Keep the existing composition. Start with clear glass; a polished opaque
dragon can be a later reflection example if the existing view shows a useful
difference. Neither extra props nor a floor are needed to establish the test.

| Comparison | Existing forward approximation | Proposed bounded RT demo |
| --- | --- | --- |
| Reflection | Shared cached environment specular lighting | Actual glass reflection directions can encounter the dragon or slab |
| Transmission | Estimated solid-volume exit and screen-space opaque radiance, with environment fallback | Follow real mesh interfaces and local scene hits, including repeated entry/exit and TIR |
| Lighting | Shared direct sun and cached environment PBR | Shared direct sun and cached PBR at opaque ray hits; no diffuse GI |
| Presentation | Interactive fixed-quality frames | Interactive fixed-quality frames; no progressive wait or offline capture advantage |

These are capabilities to inspect, not a prediction that every view will show
a large improvement. A distant sky can look similar in both. Prefer two
authored views: the whole scene and a body/base detail where marble veins can
be seen through glass. Replay the same orbit, including a view where relevant
base geometry leaves the camera image. Check local reflection visibility,
refracted marble placement, thickness/tint and continuity through camera motion.

Avoid a universal claim that raster rendering cannot make these images.
Screen-space methods only have camera-visible data; reflection captures can
include off-screen geometry but approximate visibility from their capture
locations. Extra raster passes and specialized representations offer further
tradeoffs. The comparison concerns Cubey's named implementations and budgets.
See [Epic's reflection-method overview](https://dev.epicgames.com/documentation/unreal-engine/reflections-environment-in-unreal-engine?lang=en-US).

The existing three-renderer material gallery supplies a useful control, but
does not already supply a forward dragon scene. Current code reuse points are
`ForwardPbrRenderer3DSceneResources` (mesh/material tables), the glTF viewer's
use of that renderer, and the shared forward shader's screen/environment
transmission routes. The forward shader estimates the second interface using
a solid-sphere approximation; this is also a documented
[Filament tradeoff](https://google.github.io/filament/main/materials.html#materialdefinitions/materialblock/refractiontype).
It is not an exact boundary solver for arbitrary dragon geometry.

Before promising a full comparison, prove that a narrow scene adapter can feed
the existing dragon and slab into the shared forward renderer. Use the existing
solid-volume approximation, with an explicitly documented thickness setting
held fixed across views. Do not add a thickness-baking project or a new
refraction solver. If the baseline is grossly broken, stop and report the
integration/approximation limit rather than counting it as an RT victory.
An environment-only RT toggle is not a substitute: it still traces interfaces
and must not be labeled non-RT.

Paired saved-view captures and matched native recordings are sufficient for
the first comparison; a live renderer-switch UI is optional later. Match the
spatial antialiasing quality target and disclose implementation differences.
Use common shadow settings (both off if no matched raster shadow path is
available) for the optics comparison; absent raster shadows are not evidence
of an intrinsic RT advantage. Record each renderer's actual GPU cost and any
visible quality tradeoffs, without forcing equal cost or assuming RT wins.

## Research basis and important caveat

[Whitted ray tracing](https://www.pbr-book.org/3ed-2018/Introduction/pbrt_System_Overview#AnIntegratorforWhittedRayTracing)
evaluates sharp reflected and transmitted branches explicitly and bounds the
recursion. This provides the basic transport pattern. Work must remain bounded;
fully expanding both branches at every event is not a practical promise.

Using environment lighting for rougher surfaces is also an established hybrid
tradeoff. NVIDIA's [UE4 integration discussion](https://developer.nvidia.com/blog/?p=17382)
describes routing rough reflections to cube maps. This is a historical precedent,
not a proposal to adopt that engine or its SDKs. Cubey already has the required
shared environment/PBR branch.

The [UE4.27 refraction documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/real-time-ray-tracing?application_version=4.27)
also shows why a shallow universal bounce limit can leave glass dark: paths
need enough interfaces to escape. Do not replace the dragon's current
long-path ceiling with a two-interface assumption.

Crucially, **deterministic branching alone is not a demonstrated cure**. Two
previous bright-path replays contained only total internal reflections between
entry and exit. Their small-sun hits vary within the camera pixel. That evidence
is illustrative, not a census of the moving grain. The first candidate therefore
changes the secondary sky lookup and camera sampling as well as branching.
Residual aliasing may remain; a promise of perfectly clean glass would repeat
the scope mistake.

## One proposed candidate

| Part | Trial policy | Deliberate tradeoff |
| --- | --- | --- |
| Geometry and interfaces | Keep the certified mesh, safe spawning, medium/facing checks, smooth interfaces, Fresnel, Snell, TIR and Beer absorption | No new optical-normal model |
| Glass branches | Evaluate reflected and transmitted branches deterministically; TIR follows its sole valid branch | Bounded approximation rather than a sampled estimator |
| Work bounds | Retain current event/query/stack ceilings initially; prune branches below a fixed 1% maximum-RGB throughput threshold | Faint paths can lose energy; record policy pruning separately from invalid transport |
| Opaque hits | End the traced chain with shared direct-sun, diffuse environment and cached specular PBR shading | Rough opaque surfaces do not launch further stochastic scene-reflection chains |
| Escaped rays | Use the shared visual sky/probe lookup; omit the separate narrow, analytic solar-radiance injection in this mode | Razor-thin solar glints will be softer/weaker; this is not an energy-preserving filter |
| Camera sampling | Four fixed 2×2 pixel samples in every displayed frame, averaged in HDR | Higher fixed frame cost and possible ordinary spatial aliasing |
| History | No progressive settling in demo mode; every frame is complete at its fixed sample count | No temporal reconstruction or long accumulated still advantage |

The numerical threshold and four-sample grid are proposed starting values for
one trial, not established optimal settings or user-approved defaults.
The grid is regular and shared across pixels; freezing a random seed is not a
substitute for removing stochastic transport choices.

Sun direction, size, intensity, visible background and atmosphere assets stay
unchanged. The explicit secondary-lighting approximation is what changes.
Cap 100 remains available. The demo must still trace actual scene geometry:
switching all glass to environment-only lookups would miss the point.

The cached opaque shading should reuse/factor the current workshop environment
branch, preserving procedural materials and the marble's independent role.
The older deterministic RT path demonstrates the structure, but its simpler
shading/medium code must not replace the qualified dragon implementation.

## Integration boundary

Proposed public control: one `--transport-mode demo|sampled` choice, restricted
to `dragon-optics` for the first pass. The ordinary default remains sampled
until visual review; an opt-in demo preset provides a concrete candidate.
Native edits and saved views retain the mode. No additional tuning sliders are
needed initially.

The fixed demo sample count must be reported as **samples per frame**, not as
progress toward the sampled mode's 256-sample target. Hide the progressive
target control while demo mode is active. Native, PNG and video paths must use
the same fixed-sample policy; offline capture must not quietly accumulate a
cleaner version of the moving demo.

Preserve the sampled route, its raw cap-zero option, existing fixtures,
material-workshop comparisons, model certificate and material recipes. No new
SDK, render graph or scene format is required.

## Bounded implementation and review

1. Implement the one opt-in candidate, reusing the current geometry and material
   infrastructure. Check finite output, valid medium handling and honest query/
   policy-termination diagnostics. Policy-pruned energy must not be disguised as
   zero-error physical transport, and invalid inside misses must not become sky.
   At a fixed pose/light state, verify seed independence and identical repeated
   frames; those checks alone do not establish acceptable moving appearance.
2. Capture a whole-scene view and head detail, then replay the same outward /
   stop / return native orbit at 1080p and normal speed. Compare against the
   retained sampled/cap-100 recording. Confirm that the first frame after moving
   has the same fixed sample quality as a stopped frame.
3. Judge visible grain, shimmer, glass clarity, refracted marble, silhouettes
   and obvious dark holes. Measure trace/whole-frame GPU cost separately from
   virtual-display video cadence. A provisional cost target is 33 ms GPU/frame
   on the RTX 5070 Ti, not a claim of desktop FPS.
4. If four samples are too costly, allow one two-sample candidate. Otherwise
   stop and present the tradeoff. If there is still distracting noise or broken
   glass, report that result; do not silently add denoising, a bigger branch
   search, new geometry repair or a footprint-research project.

Accept noticeable simplifications if the demo remains convincing and moving
quality improves substantially. Promote a default only after reviewing the
actual recording. The existing model/base/lighting composition stays intact;
no commit, push or site publication is part of this research pass.
