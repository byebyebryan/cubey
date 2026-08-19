# Render Graph Direction

This note maps Cubey's render graph direction before the renderer grows more
pass-heavy. It is both a design checkpoint and the current implementation
boundary for declaration, validation, synchronous execution, and explicit
sync-requirement derivation. The near-term goal is to keep
upcoming renderer, material, shadow, postprocess, readback, and async work
compatible with a graph path without forcing simple direct-command examples
through it prematurely.

## Why Map It Now

Modern explicit graphics APIs make the application responsible for resource
usage, synchronization, layout transitions, queue ownership, and lifetime.
Render graphs exist because those decisions become hard to maintain when a
frame contains many dependent passes.

Cubey already has the early pressure cases:

- `examples/shadow_cube` records a depth pass, transitions the depth texture
  for sampling, then records the camera color pass.
- `projects/fluid/smoke_2d` records an ordered compute pipeline with repeated storage
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
still own pipeline selection, descriptor policy, pass order, and pass-local
render intent. Graph-backed examples delegate command-buffer begin/end and
per-frame graph resource ownership to the render graph frame executor; simpler
examples can still record commands directly.

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
pass index, declared resources, and, when supplied, the active
`CommandRecorder`. Missing callbacks fail at execute time, not compile time, so
declaration-only tests and diagnostics can still compile a graph without
recording work. Recorder-less execution remains valid until a pass asks for
`context.recorder()`, which fails clearly.

`RenderGraphResourceSet` can bind resolved resources and create simple
non-aliased transient textures/buffers for graph-created resources.
`RenderGraphFrameResources` owns one resource set per frame slot so examples
can replace only the slot whose fence has already been waited.
`RenderGraphFrameExecutor` wraps that slot ownership with command-buffer
begin/end and recorder-aware graph execution. Its prepare hook runs after graph
resources are allocated and before command recording, so descriptor updates can
point at graph-created images. Resolved target and sampled-texture helpers
translate graph texture declarations into dynamic-rendering target views or
descriptor-ready image/view/layout triples. Recorder-backed graph execution now
records each pass's before/after derived requirements through `CommandRecorder`
around the pass callback; this is graph-owned synchronization, not hidden render
policy.

It does not allocate descriptors, reorder passes, cull passes, alias transient
memory, or schedule async work. `examples/shadow_cube` was the first multipass
reference migration, and `projects/fluid/smoke_2d` first exercised the coarse
simulation-compute to fullscreen-render boundary. The graph is now also used by
`ForwardPbrRenderer3D`, atmosphere, cloud reference, ocean, terrain, Water 2D,
Water 3D, and the shared Pyro 3D renderer. These consumers use graph-owned
declared pass-boundary synchronization while keeping solver-internal barriers,
descriptors, pipelines, and render intent explicit.

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
    .execute([](const RenderGraphExecutionContext& ctx) {
        const auto& recorder = ctx.recorder();
        // Record depth-only commands with app-owned resources.
    });

graph.add_pass("scene")
    .read_texture(shadow_map)
    .write_color(scene_color)
    .write_depth(depth)
    .execute([](const RenderGraphExecutionContext& ctx) {
        const auto& recorder = ctx.recorder();
        auto target = resolved_color_target_view(ctx, scene_color);
        // Record color pass commands into the graph-created target.
    });

graph.add_pass("present")
    .read_texture(scene_color)
    .write_color(backbuffer)
    .execute([](const RenderGraphExecutionContext& ctx) {
        const auto& recorder = ctx.recorder();
        // Record fullscreen present/copy commands with app-owned descriptors.
    });

graph_executor.record({
    .device = &device,
    .command_buffer = frame.command_buffer,
    .frame_slot = frame.frame_slot,
    .label = "vkEndCommandBuffer example",
}, compiled_graph);
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
6. Initial `shadow_cube` migration to execute shadow and scene pass bodies
   through the graph shell.
7. In-graph sync requirement derivation for texture transitions and buffer
   barriers, with recorder-backed execution recording them through
   `CommandRecorder`.
8. Coarse `smoke_2d` simulation-to-render graph declaration.
9. Imported initial/final resource state, transient first-use transitions,
   execution-time resource resolution, and non-aliased transient allocation.
10. `shadow_cube` transient scene-color allocation, resolved color target views,
    and a graph-declared fullscreen present pass.
11. Per-frame-slot graph resource ownership and sampled texture view resolution.
12. Recorder-aware graph execution plus `RenderGraphFrameExecutor`, first used
    by `shadow_cube` and `smoke_2d` to keep app `record_frame` callbacks focused
    on building per-frame data and graph declarations.
13. Adoption by `ForwardPbrRenderer3D`, atmosphere, cloud reference, ocean,
    terrain, Water 2D, Water 3D, and Pyro 3D without adding scheduler, aliasing,
    or automatic descriptor policy.

This keeps the graph as a validation, vocabulary, pass-ordering, and
sync-requirement shell rather than a renderer rewrite. Barriers stay explicit in
compiled graph data, and recorder-backed execution records them around pass
callbacks.

## Further Expansion Triggers

The declaration/execution boundary is already broadly adopted. Expand its
feature set only when a current consumer demonstrates one of these needs:

- intermediate-resource readback or capture cannot remain project-local;
- a deferred/G-buffer or longer postprocess chain needs attachment lifetime
  analysis beyond the current ordered graph;
- transient-resource memory pressure justifies measured aliasing work;
- optional pass culling has a concrete performance or maintenance benefit;
- compute/render interop crosses queue families and makes split-queue ownership
  or async scheduling concrete;
- repeated descriptor preparation around graph-created resources proves a
  narrow descriptor-facing contract.

## Deferred Complexity

Do not fold these into the current minimal graph boundary:

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
