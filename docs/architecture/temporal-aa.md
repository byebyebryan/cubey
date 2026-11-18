# Native temporal anti-aliasing

The TAA foundation resolves scene-linear HDR before display exposure and
tone mapping. `cubey::render::TemporalAntiAliasing` owns the common motion and
resolve passes, history images, uniforms and descriptor slots. Producers supply
a coherent view and either depth or their own motion field. Forward can overlay
rigid-object motion before the resolve. SDF uses the same runtime independently
of the forward renderer.

## Application policy

Projects share `--anti-aliasing off|auto|taa`. On the dogfood branch, Auto
requests TAA for interactive views. An adapter admits the current frame before
applying jitter or allocating history. Headless Auto uses the direct render
path. A headless beauty capture can explicitly select TAA and define its
history warm-up. Reference captures and raw diagnostics use direct output.
The lower-level forward renderer defaults to Off for callers that supply their
own settings. DLSS, temporal upscaling and denoising are deferred.

The CLI uses one flag across projects. Saved SDF and ray tracing configs keep
`anti-aliasing`, and the workshop and other projects use `anti_aliasing`.
Launch status reports the requested policy, effective mode, and bypass reason.
Profiles that use the temporal adapter retain the resolved request and
effective mode alongside history age, reset reason, and jitter.

## View and motion contract

Each runtime owns one independently rendered view. Its input/output extent is
identical. The supported depth convention is Vulkan forward Z in `[0,1]`, with
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
