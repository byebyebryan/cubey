# DLSS Super Resolution qualification

The glTF Viewer accepts `--upscaler dlss-quality` on a build configured with
the pinned [NVIDIA DLSS v310.9.16 SDK](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.16),
commit `9981c6350dc58c0ce21f024fd65e31e851c31689`. The
[Viewer guide](../../projects/gltf_viewer/README.md#dlss-quality-on-native-linux)
has the build and launch commands. Explicit selection requires an opaque,
rigid view under static IBL. A masked test asset was rejected with the
reason `alpha mask/blend`. The SDK-free build reports the configure option
when DLSS is requested. Both builds produced byte-identical pairs of images
with ordinary headless Auto and Off.

## Vulkan and temporal inputs

NGX discovers its instance extensions and queries each candidate NVIDIA
adapter before device creation. The selected adapter must support Vulkan
buffer device address. The host owns context initialization, evaluation,
and release on its GPU work thread. A retired render target can be replaced
after the host completes outstanding frames. Vendor SDK files stay local.

The Viewer explicitly selects DLSS before creating its targets. NGX Quality
sets the render extent. Scene-linear HDR, forward-Z depth and RG16F motion
use that extent. The shared camera-motion and rigid-motion passes supply
previous-minus-current vectors in render pixels. NGX receives the negative
of Cubey's sample offset because it expects the projected image displacement.
DLSS writes display-size scene-linear HDR before tone mapping and UI. NGX
clears its output with a transfer command and then writes it from compute.
The graph describes both writes, and a queue memory barrier covers history
owned by the SDK. The host commits Cubey's view state only after submission.

## Native evidence

The host was a GeForce RTX 5070 Ti with driver 620.70.06. A standalone Vulkan
probe evaluated 20 frames each of Quality and native-size DLAA with finite
HDR readback and synchronization validation. Matched jitter and motion sign
controls favored the adapter conventions above. The Viewer exposes Quality.

For the Viewer, the same opaque 871k-triangle dragon and static lighting
were rendered with Off, native TAA and DLSS Quality. Each matched 1440p
orbit encoded 96 frames with synchronization validation. DLSS reconstructed
1707×960 to 2560×1440. The assembled comparison shows similar contours and
some differences in fine specular detail. This visual check uses sampled
frames of the captured orbit.

The native X11 runs exercised orbit, stop, a camera-home cut, and resize.
DLSS accepted 99 frames and TAA accepted 101. Both reset on first use,
asset activation, the explicit cut and resize. DLSS moved from a 600×667
render extent at 900×1000 display to 800×520 at 1200×780 display. The
profiler starts a new window-frame sequence on swapchain recreation. The
receipt identifies resets in submission order. Physical desktop visual
acceptance remains open.

Each timing figure is a whole-scene GPU p95 from a steady headless Release
run on the same asset. The first 24 of 120 frames were excluded, and the
paired runs were repeated in reverse order.

| Display extent | Native TAA | DLSS Quality |
| --- | ---: | ---: |
| 2560×1440 | 0.81 ms | 1.36 ms |
| 3840×2160 | 1.24 ms | 2.40 ms |

At 1440p, Quality reduced the main scene pass from 0.48 to 0.33 ms. NGX
evaluation was 0.84 ms. At 2160p it was 1.66–1.67 ms. Reconstruction cost
exceeded the shading savings in this forward scene. Native TAA is the
Viewer's Auto choice.

## Next producers

The analytic SDF route already computes forward depth and temporal motion.
Its first trial needs trace and post targets with separate extents, preserved
cut and edit semantics, and paired motion and cost at an equal output size.
Fixed opaque RT needs depth from the same jittered primary hit used for its
motion vector, with the same sky convention and a separately measured ray
budget. These pixel-heavy routes are stronger candidates for measuring
whether the cost of reconstruction pays off.

Retained local evidence is under `outputs/dlss/sr-20261118/`, including the
pinned NGX source and readback probe, paired orbit clips, window screenshots,
per-pass profiles, rejection logs, and reproduction scripts.
