# Render Graph Direction

This note maps Cubey's render graph direction before the renderer grows more
pass-heavy. It is both a design checkpoint and the current implementation
boundary for declaration, validation, synchronous execution, and explicit
sync-requirement derivation. The near-term goal is to keep
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
graphics, compute, and transfer passes; attach optional material pass metadata
and pass callbacks; and compile those declarations into a validated
`CompiledRenderGraph`.

The compiled graph preserves pass insertion order and resource usage
declarations. It validates graph-local handles, imported versus transient
resource availability, queue-domain restrictions, attachment/storage aspect
rules, incompatible same-pass resource access, explicit imported resource
state, transient first use, and texture/buffer sync requirements for
read-after-write, write-after-read, and write-after-write hazards.

`CompiledRenderGraph::execute()` runs each compiled pass callback in order and
passes a `RenderGraphExecutionContext` that exposes the graph, current pass,
pass index, and declared resources. Missing callbacks fail at execute time, not
compile time, so declaration-only tests and diagnostics can still compile a
graph without recording work. The callback body still captures the app-owned
command recorder, pipelines, descriptors, and app-specific resource owners it
needs. `RenderGraphResourceSet` can bind resolved resources and create simple
non-aliased transient textures/buffers for graph-created resources.
`RenderGraphFrameResources` owns one resource set per frame slot so examples
can replace only the slot whose fence has already been waited. Resolved target
and sampled-texture helpers translate graph texture declarations into
dynamic-rendering target views or descriptor-ready image/view/layout triples.
`record_render_graph_barriers` records a pass's before/after derived
requirements through `CommandRecorder`; this is explicit command recording,
not hidden graph execution.

It does not allocate descriptors, reorder passes, cull passes, alias transient
memory, or schedule async work. `examples/shadow_cube` is the first reference
migration: the graph now executes shadow, scene, and present pass callbacks,
allocates a non-aliased transient scene color target, resolves that target into
dynamic-rendering and descriptor inputs during execution, and records
shadow-depth, scene-depth, scene-color, backbuffer acquire, and present release
transitions from graph-derived requirements. `projects/fluid_2d` declares a
coarse simulation-compute to fullscreen-render graph and uses graph-derived
buffer barriers plus backbuffer acquire/release transitions while keeping
solver-internal barriers manual.

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
  execution. The first implementation can allocate non-aliased Vulkan image or
  buffer resources through `RenderGraphResourceSet`.
- **Imported resource state**: optional initial/final texture or buffer state
  declared with an imported resource so the graph can derive acquire and
  release barriers at frame boundaries.
- **Usage declaration**: a pass statement that it reads or writes a resource as
  color attachment, depth attachment, sampled input, storage input/output,
  or transfer source/destination. Present remains host/swapchain-owned, but a
  swapchain target can declare a final present state for graph-derived release.
- **Compile**: validation and derivation of pass order, declared resource
  lifetime, optional material pass metadata, and in-graph sync requirements
  from graph declarations.
- **Execute**: synchronous pass-callback invocation in compiled pass order.
  Callbacks receive declaration context and record Vulkan commands explicitly.
- **Queue domain**: the queue class a pass expects, initially graphics or
  compute. Split queues remain future work.

## Intended Shape

The eventual API should look like a setup/execute split:

```cpp
auto backbuffer = graph.import_color_target("swapchain", frame.color_target);
auto depth = graph.import_depth_target("depth", depth_target);
auto shadow_map = graph.import_depth_target("shadow map", shadow_target);
auto scene_color = graph.create_texture(scene_color_desc);

graph.add_pass("shadow")
    .write_depth(shadow_map)
    .execute([&recorder](const RenderGraphExecutionContext& ctx) {
        // Record depth-only commands with app-owned resources.
    });

graph.add_pass("scene")
    .read_texture(shadow_map)
    .write_color(scene_color)
    .write_depth(depth)
    .execute([&recorder](const RenderGraphExecutionContext& ctx) {
        record_render_graph_barriers(recorder, ctx, RenderGraphBarrierPhase::BeforePass);
        auto target = resolved_color_target_view(ctx, scene_color);
        // Record color pass commands into the graph-created target.
    });

graph.add_pass("present")
    .read_texture(scene_color)
    .write_color(backbuffer)
    .execute([&recorder](const RenderGraphExecutionContext& ctx) {
        record_render_graph_barriers(recorder, ctx, RenderGraphBarrierPhase::BeforePass);
        // Record fullscreen present/copy commands with app-owned descriptors.
        record_render_graph_barriers(recorder, ctx, RenderGraphBarrierPhase::AfterPass);
    });
```

The important contract is that setup declares resource use before execution
records commands. That matches the established pattern in Filament FrameGraph
and Unity Render Graph while preserving Cubey's Vulkan-first command recording.
`shadow_cube` now exercises this declaration and execution shape for a
depth-only shadow pass, a scene pass into a graph-created color target, and a
fullscreen present pass while still recording commands explicitly.

## Implementation Slices

Completed slices:

1. Declaration types and unit tests.
2. Imported color/depth targets, imported textures, transient textures, and
   buffer handles.
3. Preserved pass insertion order in compile output.
4. Validation for missing resources, invalid handles, read-before-write for
   non-imported resources, and duplicate incompatible same-pass access.
5. Synchronous execution callbacks on compiled passes.
6. Initial `shadow_cube` migration to execute the shadow and scene pass bodies
   through the graph shell.
7. In-graph sync requirement derivation for texture transitions and buffer
   barriers, with explicit recording through `CommandRecorder`.
8. Coarse `fluid_2d` simulation-to-render graph declaration.
9. Imported initial/final resource state, transient first-use transitions,
   execution-time resource resolution, and non-aliased transient allocation.
10. `shadow_cube` transient scene-color allocation, resolved color target views,
    and a graph-declared fullscreen present pass.
11. Per-frame-slot graph resource ownership and sampled texture view resolution.

This keeps the graph as a validation, vocabulary, pass-ordering, and
sync-requirement shell rather than a renderer rewrite. Barriers stay explicit:
the graph derives named requirements, and command callbacks decide where to
record them.

## Adoption Triggers

Expand graph code when at least one of these becomes active work:

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
