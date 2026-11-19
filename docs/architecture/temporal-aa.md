# Temporal anti-aliasing and upscaling

The TAA foundation resolves scene-linear HDR before display exposure and
tone mapping. `cubey::render::TemporalAntiAliasing` owns the common motion and
resolve passes, history images, uniforms and descriptor slots. Forward PBR,
SDF and fixed opaque RT use the shared resolve. Forward PBR can overlay
rigid-object motion. The producers also share the temporal inputs used by
the optional NGX backend.

## Application policy

Projects share `--anti-aliasing off|auto|taa`. Auto is the application
default and requests TAA for interactive views. Each producer checks whether
the current view can supply valid temporal inputs before applying jitter or
allocating history. Headless Auto uses the direct render path. A headless
beauty capture can explicitly select TAA and define its history warm-up.
Reference captures and raw diagnostics use direct output. The lower-level
forward renderer defaults to Off for callers that supply their own settings.

### Upscaler selection

`--upscaler native` is the default on the Viewer, SDF and ray tracing
launch paths. Native rendering follows each app's AA policy. Select
`--upscaler dlss-quality` at launch for a qualified path. The app requests
NGX's Vulkan capabilities at device creation and uses one reconstruction
pass. Quality uses preset K. The Viewer also exposes M and L as experimental
choices.

| Path | Native Auto | Explicit DLSS Quality |
| --- | --- | --- |
| Eligible opaque Viewer | TAA in interactive views | K with static IBL and rigid opaque geometry |
| Opaque PBR Materials specimens | TAA in eligible interactive views with frozen lighting | — |
| Eligible opaque SDF | TAA in interactive 1× views | K on the analytic path at 1× with frozen lighting and clouds disabled |
| Fixed opaque RT | TAA in interactive views | K for the qualified bronze dragon |
| Certified fixed glass | Four primary samples per render pixel in the showcase launcher | Experimental K through the glass launcher |
| Cloud paths | Native rendering and the project's cloud history policy | Parked headless studies |
| Projects without a compatible producer | Direct rendering | — |

The compatible opaque launch examples and their measured limits are in the
[Viewer guide](../../projects/gltf_viewer/README.md#dlss-quality-on-native-linux),
[SDF guide](../../projects/sdf/README.md#quality-upscaling), and
[fixed opaque RT guide](../../projects/raytracing/docs/fixed-opaque-taa.md).
The certified glass launcher is documented in the
[ray tracing project guide](../../projects/raytracing/README.md).

The CLI uses one flag across projects. Saved SDF and ray tracing configs keep
`anti-aliasing`, and the workshop and other projects use `anti_aliasing`.
Launch status reports the requested policy, effective mode, and bypass reason.
Profiles that use the temporal adapter retain the resolved request and
effective mode alongside history age, reset reason, and jitter.

## Integrating a new project

Compose `host::common_run_config_schema()` to inherit the AA option. Use
`host::native_taa_requested()` to resolve the request and
`host::report_anti_aliasing()` to report actual admission. Projects without
temporal inputs can use the shared option and report why they render directly.
Keep upscaler selection in the project schema.

### Projects using Forward PBR

1. Register the shader sources and dependencies with
   `cubey_forward_pbr_shader_sources()` and `cubey_forward_pbr_shader_depends()`.
   Load the generated files with
   `forward_pbr_renderer_3d_config_from_shader_directory()`.
2. Map the resolved AA request to `ForwardPbrRenderer3DSettings::anti_aliasing`.
   Keep draw entity IDs stable. Identify the view with its camera entity or
   `temporal_view_id`. Use `temporal_appearance_static` for the scene's
   lighting and appearance policy, `temporal_reset` for explicit cuts, and
   `temporal_content_generation` for discontinuous content changes. The
   renderer checks admission and owns jitter, motion and resolve.
3. Connect the windowed and headless host callbacks to the renderer's
   `frame_submitted(ticket)` and `frame_discarded()`. Read
   `temporal_status()` for the effective mode and bypass reason.

Use the [PBR Materials workshop](../../projects/pbr_materials/pbr_materials_app.cpp)
for native TAA wiring and the
[glTF Viewer](../../projects/gltf_viewer/gltf_viewer_render.cpp) for an upscaling
target in the same renderer.

### Projects with their own render pass

Use one `TemporalAntiAliasing` per view and the shared resolve shader. For
camera-only motion over a static scene, compile the shared motion shader and
use `declare_camera_motion()` with the producer's depth. If the renderer
already emits motion, import its write state into the graph and use
`declare_resolve_with_motion()`. Render with the candidate's jitter and feed
the resulting HDR output to display post. SDF provides the
[depth-derived example](../../projects/sdf/sdf_renderer.cpp), and fixed opaque
RT provides the [ray-generated example](../../projects/raytracing/raytracing_renderer.cpp).
Follow the [view contract](#view-and-motion-contract) and
[submission lifecycle](#submission-and-resource-lifetime) below.

### Adding optional DLSS

Link `cubey::dlss` and reuse the SDK setup in the
[Viewer build instructions](../../projects/gltf_viewer/README.md#dlss-quality-on-native-linux).
For an explicit request, pass `host::DlssUpscaler::vulkan_requirements()` to the
host before creating the Vulkan instance and device. Create the backend and
its target on the GPU owner. `create_target()` returns the render extent for
the requested display extent. Retire affected submissions before recreating
the targets or destroying the backend. Reject explicit requests for scenes
that cannot supply the required inputs. SDK-free builds keep the native
path and report missing support on explicit DLSS requests.

Forward PBR takes the backend and render extent in
`ForwardPbrRenderer3DTargetResourcesInfo`. Other producers use
`TemporalViewState` to prepare and publish accepted views. Supply scene-linear
color, depth and motion at the render extent, the prepared view, the output
extent and frame time in milliseconds through `TemporalUpscaleInputs`. Use the
HDR output of `TemporalUpscaler::declare()` for display post.
`TemporalCameraMotion` can supply motion from depth for static scenes. Both
integrations use one reconstruction pass per view.

Qualify a new consumer with matched Off and temporal captures through
hold, orbit, stop, cut and resize. Check history resets and synchronization,
and exercise a bypassed scene. For DLSS, also check the SDK-free path and
compare timing and image quality with a spatial control at the same render
extent. Cloud composition, deformation and transmission need
producer-specific evidence before admission.

## View and motion contract

Each runtime owns one independently rendered view. Native TAA uses identical
render and display extents. Depth uses Vulkan forward Z in `[0,1]`, with
clear/miss equal to 1. Descriptor setup rejects mismatched color, depth, and
motion extents. The resolve reads finite scene-linear HDR RGB and writes
resolved beauty with alpha 1.

Motion is **previous minus current unjittered pixel position**, with positive Y
down. The current depth belongs to the jittered color view. Camera motion
reconstructs a world point through the inverse current jittered view-projection,
then projects through current and previous unjittered matrices. Clear-depth
pixels use a world direction (`w=0`), excluding camera translation from sky
motion. Rigid motion supplies previous and current world transforms for the
same stable draw identity. Zero motion is valid. Unknown previous surfaces use
the finite vector `extent + 16`, forcing history bounds rejection.
Geometry clipped by the previous near/far planes also uses this invalid vector;
the clear sky direction is exempt from the geometry depth-range test.

`sample_offset_pixels` means raster pixel `p` samples unjittered position `p+j`.
The projection therefore shifts geometry by `-j`, or `-2*j/extent` in clip
space. The sequence is centered Halton(2,3), 16 phases. Ordinary camera motion,
object motion and jitter do not invalidate history. Shadow rendering keeps the
unjittered view.

## Submission and resource lifetime

The CPU state supports one pending candidate:

1. `begin_frame(slot, view)` prepares matrices, jitter and history indices.
2. Declare camera motion, optional rigid overlay, and resolve in the graph.
   A renderer that writes motion directly leaves `TemporalAaConfig::motion_shader`
   empty and calls `declare_resolve_with_motion` with its native-size RG16F
   texture. It declares the producer's write state and owns that image.
3. Prepare descriptors from the compiled graph's resolved resource set.
4. Record and submit the frame through its host.
5. Call `commit_frame(token, actual_submission_ticket)` after queue acceptance,
   or `discard_frame()` if recording/submission failed.

Preparing and recording do not publish history. Discarding advances neither the
accepted sequence nor its matrices; the next candidate reseeds. Tickets must be
nonzero and newer than the accepted ticket. They belong to the producer's GPU
submission owner, not a capture readback or simulation update. Hosts expose
optional submitted/discarded callbacks. An accepted submission remains accepted
when presentation later fails.

Two persistent RGBA16F PQ history images alternate by accepted view sequence,
independent of CPU frame slots and swapchain image indices. Uniforms and
descriptor sets follow retired frame slots. Motion (RG16F) and resolved HDR
(RGBA16F) are graph transients per slot. First-use initialization is recorded in
the candidate graph; initialized image state is published only at commit.
Same-queue compute barriers cover history dependencies across submissions.

The caller retires the runtime's last real use before replacement or destruction.
Creation builds a complete replacement before exchanging ownership, so failure
preserves the previous generation. Replacement with a pending candidate is
rejected. There are no new per-frame queue-idle waits.

## Reset and admission

History resets for first use, explicit cuts/restarts/seeks, projection or extent
changes, view identity, producer content generation, discarded candidates, and
returning from Off or an unsupported diagnostic path. Post-only exposure does
not reset scene-linear history. Producers increment content generation for
material, scene roster and discontinuous environment changes.

Initial admission is opaque rigid geometry and clear sky. Masked/translucent
materials, transmission, deformation, cloud composition and dynamic water need
additional motion/appearance contracts. Raw diagnostics bypass TAA and jitter.
Requested/effective AA and a bypass reason belong to each producer; the common
runtime reports actual TAA age, reset, jitter and requested resource bytes.

Resource metrics separate texel/uniform payload sizes from Vulkan memory
requirements for the dedicated images and uniform buffers. The transient image
requirements are measured from the first resolved graph generation. Driver
descriptor/pipeline storage and whole-process residency are excluded; these
values are not a peak VRAM claim.
With producer-supplied motion, the shared runtime omits its camera-motion pass
and reports its own textures and uniforms. The producer accounts for its motion
image separately.

Fixed opaque RT uses the producer-motion entry point. It exports motion from
the first existing primary lane and applies one jitter offset to all primary
lanes. RT sampling restarts per view while TAA keeps independently committed
history. The [consumer results](../notes/temporal-consumers-2026-11.md) also
cover SDF geometry cuts and the separate cloud history.

SDF bypass reason IDs are 0 active, 1 diagnostics, 2 supersampling other than
1x, 3 clouds, 4 transmission, 5 raw query/quality capture, 6 edit/cutter preview,
7 dynamic lighting, and 8 unsupported device. Matched opt-in
presets live in `projects/sdf/presets/taa-foundation-2026-11/`.

## Resolve provenance and qualification

The shader ports a native-resolution subset of the pinned public MIT Donut TAA
core: PQ-domain feedback, 3x3 variance clipping, longest-neighborhood motion,
Catmull-Rom history sampling and fixed blending. Upscaling and clamp-relaxation
masks are excluded. Cubey adds finite input guards and an explicit viewport
inset. See [provenance and license](../../third_party/temporal_aa/README.md).
The executable DLSS sample is comparison evidence; its bundled source is not
the MIT donor.

Qualification fixes new-frame weight at 0.1, variance factor at 1 and the
radiance input limit at 10000. Other `TemporalAaConfig` filter constants need
their own finite-HDR and quality checks. Applications expose the AA policy
with these fixed constants.

`temporal_view` checks projection/motion math and accepted-history lifecycle.
`temporal_gpu` checks actual shader output and resource lifecycle, including
discarded recordings, failed replacement, partial workgroups, finite HDR and
camera/sky motion. Slot variants exercise one, two and three frame slots.
Interleaved GPU views retain distinct color histories with unrelated submission
ticket gaps. Near/far clipping and fast vectors use separate validity and
half-float quantization checks.
`forward_temporal_gpu` also replaces only its private test resolve with a motion
probe. A static camera produces a zero stationary signal and a positive signal
for a right-moving object. This proves signed rigid vectors and depth-matched
coverage reach the actual resolve input, independently of history age or movies.
`temporal_gpu_sync` runs with synchronization validation, whose enabled state is
proved separately by the repository hazard probe. Host submission GPU tests
cover PNG, video, render-only and failure/callback paths.

Optional reference qualification uses `tools/taa/prepare_donut_reference.py`
with the exact public HLSL hash and an external DXC executable. It specializes
the donor to the native subset and records all boundary adaptations. The same
GPU fixture can dump candidate and reference half floats;
`tools/taa/compare_reference.py` compares all 24 accepted frames using
`abs(candidate-reference)/max(1,abs(reference))`, tolerance 0.005. Neither DXC nor
the donor source is required by ordinary builds.

Visual/cost acceptance follows the [implementation plan](../notes/taa-foundation-plan.md):
matched Off+SS1, TAA+SS1 and Off+SS2 observatory views, stationary history ages,
orbit/stop/return/cut, opaque workshop detail and moving rigid disocclusion.
Stationary stills do not establish motion acceptance. Encoded video, raw native
captures, validation, GPU cost and physical desktop behavior remain distinct
evidence.

The [qualified opt-in results](../notes/taa-foundation-closeout.md) and
[evidence packet](../evidence/taa-foundation-2026-11/README.md) record the exact
source, native captures, reference comparison, repository gates and remaining
quality/admission limits. They do not promote defaults or claim user aesthetic
or physical desktop acceptance.
