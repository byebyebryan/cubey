# Render Graph Direction

This note maps Cubey's render graph direction before the renderer grows more
pass-heavy. It is both a design checkpoint and the current implementation
boundary for the declaration/validation layer. The near-term goal is to keep
upcoming renderer, material, shadow, postprocess, readback, and async work
compatible with a future graph without forcing every example through a graph
executor now.

## Why Map It Now

Modern explicit graphics APIs make the application responsible for resource
usage, synchronization, layout transitions, queue ownership, and lifetime.
Render graphs exist because those decisions become hard to maintain when a
frame contains many dependent passes.

Cubey already has the early pressure cases:

- `examples/shadow_cube` records a depth pass, transitions the depth texture
  for sampling, then records the camera color pass.
- `projects/fluid_2d` records an ordered compute pipeline with repeated storage
  buffer hazards before rendering the result.
- Headless and project GPU services already expose readback/capture paths that
  will become easier to reason about when intermediate targets are declared.

The direction is worth capturing now because future renderer and material APIs
will naturally choose names for passes, targets, resources, and command
recording. Those names should not block a later graph.

## Current State

`cubey::scene::FrameRenderPlan3D` is a CPU-side pass list. It preserves explicit
pass order and pass kind for scene-derived views, but it does not declare
resource dependencies, compile a graph, own transient resources, generate
barriers, or schedule GPU work.

`cubey::render` currently owns target views, texture/depth texture wrappers,
mesh/draw helpers, frame slots, uniform buffers, and render resource handles.
`cubey::vulkan` owns Vulkan object lifetime, command recording helpers, image
transition helpers, queue submission, and GPU-owner work. Examples and projects
still own pipeline selection, descriptor policy, pass order, barriers, and
command recording sequence.

That current split is intentional. A render graph should grow above the narrow
render vocabulary, not replace the Vulkan layer or move scene policy into
`cubey::render`.

## Current Checkpoint

`cubey::render::RenderGraphBuilder` is the first graph declaration layer. It
can import swapchain/offscreen color targets, depth targets, textures, and
buffers; create transient texture and buffer declarations; declare ordered
graphics, compute, and transfer passes; and compile those declarations into a
validated `CompiledRenderGraph`.

The compiled graph preserves pass insertion order and resource usage
declarations. It validates graph-local handles, imported versus transient
resource availability, queue-domain restrictions, attachment/storage aspect
rules, and incompatible same-pass resource access.

It does not execute passes, create Vulkan resources, generate barriers, infer
layouts, allocate descriptors, reorder passes, cull passes, alias transient
memory, or migrate existing examples.

## Boundary

The future graph belongs in `cubey::render` because it is renderer-facing
vocabulary above Vulkan and below scene/project policy.

The graph should consume declarations from projects, examples, or a later
renderer. Scene helpers may provide CPU draw plans that a graph pass records,
but scene helpers should not own graph compilation. Vulkan remains the source
of truth for layouts, access masks, pipeline stages, queues, command buffers,
and synchronization correctness.

Expected dependency direction:

```text
scene/project/renderer code
        |
        v
render graph declarations in cubey::render
        |
        v
resolved resources + explicit command recording through cubey::vulkan
```

## Vocabulary

- **RenderGraph**: a per-frame declaration of passes and resources.
- **RenderGraphBuilder**: the setup-time API used to create/import resources
  and add passes.
- **RenderGraphPass**: a named unit of rendering, compute, transfer, or readback
  work.
- **Resource handle**: an opaque graph handle for a texture or buffer. Actual
  Vulkan objects are only resolved during execution.
- **Imported resource**: a graph-visible resource owned outside the graph, such
  as the swapchain image, a persistent depth texture, a material texture, or a
  temporal history target.
- **Transient resource**: a resource whose lifetime is limited to one graph
  execution.
- **Usage declaration**: a pass statement that it reads or writes a resource as
  color attachment, depth attachment, sampled input, storage input/output,
  or transfer source/destination. In v1, present remains host/swapchain-owned:
  the backbuffer is declared as a color attachment write, then explicit Vulkan
  code transitions it for presentation.
- **Compile**: validation and derivation of pass order, resource lifetime, and
  synchronization needs from declarations.
- **Execute**: command recording using resolved resources.
- **Queue domain**: the queue class a pass expects, initially graphics or
  compute. Split queues remain future work.

## Intended Shape

The eventual API should look like a setup/execute split:

```cpp
auto backbuffer = graph.import_color_target("swapchain", frame.color_target);
auto depth = graph.import_depth_target("depth", depth_target);
auto shadow_map = graph.create_texture("shadow map", shadow_desc);

graph.add_pass("shadow")
    .write_depth(shadow_map)
    .execute([](RenderGraphExecutionContext& ctx) {
        // Record depth-only commands with resolved resources.
    });

graph.add_pass("scene")
    .read_texture(shadow_map)
    .write_color(backbuffer)
    .write_depth(depth)
    .execute([](RenderGraphExecutionContext& ctx) {
        // Record color pass commands with resolved resources.
    });
```

This is illustrative vocabulary, not a committed header design. The important
contract is that setup declares resource use before execution records commands.
That matches the established pattern in Filament FrameGraph and Unity Render
Graph while preserving Cubey's Vulkan-first command recording. `shadow_cube`
now exercises this declaration shape for its depth-only shadow pass and color
scene pass while still recording commands and layout transitions manually.

## Implementation Slices

The first implementation slice is intentionally small:

1. Declaration types and unit tests only. No executor.
2. Imported color/depth targets, imported textures, transient textures, and
   buffer handles.
3. Preserved pass insertion order in compile output.
4. Validation for missing resources, invalid handles, read-before-write for
   non-imported resources, and duplicate incompatible same-pass access.
5. Pass culling disabled by default.

This makes the first code slice a validation and vocabulary layer rather than a
renderer rewrite.

Future slices should add an execution callback shell only after declaration
validation is useful on its own, then migrate `shadow_cube` or a postprocess
example as the first reference graph. Barrier generation should either stay
explicit or emit named transition requirements that still map directly to
`cubey::vulkan::ImageLayoutTransition`.

## Adoption Triggers

Implement graph code when at least one of these becomes active work:

- a second multipass example repeats shadow-map target and barrier plumbing;
- bloom, tone mapping, TAA, or another postprocess chain introduces
  intermediate color targets;
- deferred rendering or a G-buffer needs multiple interdependent attachments;
- readback or capture needs to target intermediate resources instead of the
  final swapchain/offscreen output only;
- compute/render interop grows beyond local hand-written barriers;
- optional debug passes become easier to declare than manually branch;
- split graphics/compute queues or async compute scheduling becomes concrete.

## Deferred Complexity

Do not build these into the first graph slice:

- full renderer ownership;
- material system ownership;
- broad pass reordering;
- automatic async compute scheduling;
- transient memory aliasing;
- render pass merging;
- automatic descriptor binding;
- shader reflection;
- backend-agnostic graph vocabulary;
- hidden Vulkan synchronization.

Those are real render graph features in larger engines, but they would obscure
the Cubey contracts before the pass/resource vocabulary has proven itself.

## Reference Anchors

- [Filament FrameGraph](https://google.github.io/filament/notes/framegraph.html):
  resource/pass dependency graph, setup/execute split, lifetime tracking, usage
  derivation, and import/export complexity.
- [Unity Render Graph fundamentals](https://docs.unity3d.com/Packages/com.unity.render-pipelines.core@17.0/manual/render-graph-fundamentals.html):
  explicit pass resource usage, setup/compile/execute phases, transient
  lifetime, imported resources, culling, and synchronization.
- [Unity Render Graph benefits](https://docs.unity3d.com/Packages/com.unity.render-pipelines.core@17.0/manual/render-graph-benefits.html):
  memory management, synchronization generation, and maintainability goals.
- [Unreal Rendering Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/rendering-dependency-graph?application_version=4.27):
  whole-frame dependency graph framing for modern explicit APIs.
- [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html):
  execution dependencies, memory dependencies, barriers, and layout
  transitions remain the correctness model underneath any Cubey graph.
