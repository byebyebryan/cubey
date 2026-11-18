# Temporal rendering beyond forward

The temporal foundation covers opaque surface reprojection in forward PBR and
SDF. This follow-up evaluates a cheaper SDF subject, volume reconstruction in
Atmosphere, composition across their boundary, and fixed opaque RT. Capture
recipes, metrics and logs are retained in the local qualification packet at
`outputs/taa/temporal-consumers-20261118/`. The [foundation results](taa-foundation-closeout.md)
retain the observatory comparison and the shared motion contract.

## SDF motion and a committed cut

The original opaque wood recipe from `material-presentation/wood-cut.json`
supplies a solid block and the same block with a through-cut. The analytic
trace keeps the same material across the cut. At 1280×720, Off+SS1, TAA+SS1
and Off+SS2 each submit 96 matched views. The camera holds, orbits 0.4 radians
over 30 views, and stops. The existing action-study path publishes the actual
through-cut at frame 72, without requesting an explicit TAA reset.

History ages advance from 0 to 71. The geometry publication resets history at
72 for `ContentChanged`; at 73, age is 1. Frame 72 is byte-identical to an
independent TAA seed at the same cut and pose. Synchronization validation is
clean. In a crop around the block, settled luma error against the SS2 spatial
reference is 3.23 for TAA and 5.06 for SS1 at frame 23. During the faster orbit,
frame 39 has 12.79 error with TAA and 5.40 with SS1. Grain and edges soften in
motion. The settled improvement returns after stopping.

The render-only GPU median/p95 at 720p is 0.321/0.356 ms for Off+SS1,
0.368/0.444 ms for TAA+SS1, and 1.042/1.214 ms for Off+SS2. These timings
exclude PNG readback and video encoding. The cut's CPU publication and test
readback overhead are measured separately from GPU pass time.

## Cloud history and sampling

Atmosphere's opt-in `--cloud-submitted-history` uses the existing cloud
radiance/transmittance and metadata resolve. The history images and previous
view follow accepted submissions. Descriptors and uniforms still follow CPU
slots. The legacy mode stores separate ping-pong histories for each slot.
With two slots and two globally alternating phases, the legacy history
reuses the same phase. The new history reads the previous accepted frame and
therefore mixes both phases. With three slots the legacy history mixes phases
but reads a view three frames old. Synchronization validation covers both slot
counts. The two-slot video comparison uses a test build with two encoder
slots, and the normal host retains three. Bypassing the cloud pass clears
sample reuse on submission and preserves the existing image layouts. The
two-slot lifecycle test also invalidates history between submissions. A
24-frame native window smoke on an owned Xvfb display used two slots and
read the previous frame on all 23 post-seed views with synchronization
validation clean.

Matched 96-frame recordings hold camera and lighting on the same local
broken-cumulus view. The weather is frozen, then winds at 30 m/s. A preliminary
unlocked-solar replay reset history every frame; the final comparison explicitly
pauses the solar clock and verifies history reads. The corrected mode costs
1.545/1.558 ms median/p95 for cloud passes at 720p, versus 0.909/0.914 ms for
one sample and 2.769/2.798 ms for two spatial samples. These are the frozen
run's GPU pass sums over 120 measured frames. Wind runs show similar cost.

The corrected mode's 8-bit decoded luma RMSE in the cloud crop is 0.84 frozen
and 0.86 with wind versus the two-sample comparison. One sample gives 2.65
in each case. The two-slot legacy temporal mode gives 0.82 and 0.84. The phase
fix is a chronological correctness improvement. Visually it is close to legacy
in this shot, and both temporal modes retain more frame-to-frame variation
than the stable one-sample control. Scope is one surface view and a moderate
wind test.

## Foreground and cloud composition

A private trial uses independent histories for traced foreground and clouds,
then feeds resolved foreground HDR into the existing depth-based cloud
compositor. All 96 views submit with both histories. The opaque history resets
at the geometry cut and cloud history keeps advancing. The matched cloud-only
and SS2 images expose a blue fringe in the combined candidate. The resolved
surface color mixes clear sky near the silhouette, while current sample depth
classifies each pixel as entirely opaque or sky. The trial switch was removed.
Acceptance needs a coverage signal aligned to the resolved foreground before
cloud composition can be admitted.

## Opaque fixed RT

The [fixed-opaque opt-in](../../projects/raytracing/docs/fixed-opaque-taa.md)
reuses the original bronze dragon and opaque marble support. Its first spatial
lane writes RG16F motion from the actual primary hit. A sky miss uses the world
ray direction. TAA adds the same subpixel offset to all existing lanes and
feeds the shared HDR resolve. Both paths trace four primary rays per pixel in
the matched comparison. Their per-frame RT sampling state is independent of
the TAA history, which advances after queue acceptance.

The 1280×720 captures match the camera and lighting on all 96 submitted views.
The camera orbits through frame 47, stops, then cuts at 72. TAA advances from
age 0 to 71, resets explicitly at 72, and reaches age 1 at 73. The 14-view
two-slot fixture and the 96-view three-slot videos passed synchronization
validation. Native window smokes on an owned Xvfb display also accepted 24
frames per mode at two primary rays per pixel. In the orbit, TAA reduces
speckle on the bronze and softens the horn and mouth contours. The inspected
cut frame shows no retained silhouette from the preceding pose.

Complete-frame GPU time at 720p is 5.171/5.312 ms median/p95 with Off and
5.264/5.392 ms with TAA. The separate render-only orbit measures 96 views per
mode after 24 warmup views with validation, readback and encoding disabled.
GPU time covers the ray dispatches, HDR post, and environment update.

The original forward hybrid, fixed glass and sampled glass captures at
640×360 are each byte-identical to the pre-adapter baseline at the matched
validation pose. The experiment covers primary surface and sky motion for the
fixed opaque scene. Glass and reflected radiance need their own history
guidance before expanding admission.
