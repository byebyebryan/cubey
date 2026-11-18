# Native TAA Auto dogfood

On the dogfood branch, all 16 project executables accept
`--anti-aliasing off|auto|taa`. The interactive default requests Auto. Each
consumer admits the current view before jittering it or allocating history.
`off` selects direct rendering. Headless Auto also renders directly, and
`taa` requests the admitted temporal path for an explicit beauty capture.
Saved Workshop, SDF and RT options keep their existing keys and take precedence
over the default. The certified glass comparison and raw diagnostics retain
their direct output. See the [temporal input and submission contract](../architecture/temporal-aa.md).

## Native launch matrix

Each project rendered 24 profiled frames at 320×180 through X11 on a GeForce
RTX 5070 Ti. The Auto column uses the launch default. The paired launch passed
explicit Off, which reported `effective=off` and `reason=explicit-off` in every
project. Terrain Water used the controlled frozen recording backend. The other
projects used their stock interactive configuration. Synchronization validation
was required. `tools/taa/run_auto_matrix.py` retains independent launch and
validation results and returns a failing exit status on validation errors.

| Project | Effective Auto mode and reason | Vulkan validation in both launches |
| --- | --- | --- |
| Atmosphere | Off, separate volume history | Clean |
| Fluid 2.5D | Off, producer unavailable | Swapchain acquisition dependency |
| Smoke 2D | Off, producer unavailable | Swapchain acquisition and buffer update |
| Water 2D | Off, producer unavailable | Clean |
| Water 3D | Off, producer unavailable | Clean |
| Fire 3D | Off, producer unavailable | Buffer update |
| Explosion 3D | Off, producer unavailable | Buffer update |
| Fractal 2D | Off, producer unavailable | Swapchain acquisition dependency |
| glTF Viewer | Off, clouds | Clean |
| Ocean | Off, producer unavailable | Clean |
| PBR Furnace | Off, conformance diagnostic | Swapchain acquisition and image writes |
| SDF | TAA, age 23 after 24 frames | Clean |
| Terrain | Off, producer unavailable | Clean |
| Terrain Water | Off, producer unavailable | Clean |
| PBR Materials | Off, transmission | Clean |
| Ray Tracing | Off, sampled transport | Clean |

All six flagged projects reproduced the same validation signatures and reported
counts in a detached build of `5539cce2` with matching launch settings. The two
main issues to fix are the image layout transition after swapchain acquisition
and the transfer-write dependency between repeated `vkCmdUpdateBuffer` calls.
The Furnace also reported an image write-after-write. The matrix keeps these
results visible in both policy modes.

## Admitted views

SDF retained 192 byte-identical native frames across the previous Off and TAA
camera/cut replays. The Workshop's explicit Off and headless Auto images
both matched the saved pre-rollout Off image byte for byte. In the Viewer,
the native staged import of the opaque dragon reset age after eight loading
frames. The imported view then advanced to age 23. The separate masked fixture
matched Off byte for byte. An advancing solar clock or unsettled environment
bypasses history. The Viewer uses the shared forward resolve and commits history
using the submission ticket from the active headless or windowed host.

The fixed opaque RT dragon advanced to age 224 in a Wayland window in Auto.
The matched Off window retained the same four primary lanes per frame and
reported zero temporal history. Both runs were validation-clean. In the
matched 96-frame headless clips, TAA reset to age zero at the explicit cut on
frame 72 and resumed on frame 73. During the orbit the sampled review frames
show smoother fine highlights with some softening. The raygen shader and a
focused GPU regression share the vector function. The test verifies zero
stationary motion across jittered hits, the signs of geometry motion, and sky
motion under translation and rotation.

The 720p measurements below are whole-scene GPU p95 in render-only Release runs.
The first 24 samples are discarded. The paired SDF runs retain 72 samples each
and Workshop and Viewer retain 96. Cost belongs to these scenes and this GPU.

| Paired scene | Off | TAA |
| --- | ---: | ---: |
| Workshop moving rigid fixture | 0.132 ms | 0.181 ms |
| Viewer rotating opaque dragon, static environment | 0.218 ms | 0.318 ms |
| SDF steady analytic scene | 0.358 ms | 0.441 ms |

The separate Dev render-only pair for steady fixed opaque RT measured
5.137 ms Off and 5.218 ms TAA whole-scene GPU p95 at 720p over 72 samples
each. The first Auto Wayland launch spent 4.44 s in RT pipeline setup. Later
headless launches showed 14–16 ms in both modes. This is a startup observation
from one machine with uncontrolled driver cache state.

## Rollout boundary

Keep Auto as the branch default. Review the eligible views on the physical
desktop before promoting them. Keep the explicit Off control available for
comparison and capture. The project matrix used virtual X11 windows. The
Wayland RT pair measured submissions and history on the desktop. A physical
visual review remains open.

Clouds retain their own volume history and need a validated composition with
surface history. Material transmission and RT glass need path-aware rejection.
Water, simulation renderers, alpha-tested edges and deforming geometry need
motion and appearance inputs that describe what the user sees. These cases
continue to render directly under the shared policy. The
[beyond-forward study](temporal-consumers-2026-11.md) records previous trials.

Local evidence is under `outputs/taa/auto-dogfood-20261118/`, including the
full app matrix, reference baseline, native clips, frame profiles and gate
receipts. The opaque dragon uses the certified geometry with SHA-256
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.
