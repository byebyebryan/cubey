# Threading And Async Design

This document captures Cubey's threading and asynchronous-work direction before
the first real project starts shaping the runtime. The goal is to make project
code fit a non-stalling Vulkan model early, without pretending the framework
already needs a full threaded renderer, render graph, or split-queue scheduler.

## Recommendation

Start the threading work now as an architecture boundary:

- Add a small CPU job layer behind `cubey` APIs.
- Shape uploads, readbacks, captures, and destruction as queued work.
- Keep Vulkan queue submission serialized through one render/GPU owner.
- Keep examples simple and educational; require longer-lived `projects/` to use
  the async-ready structure.
- Defer dedicated render threads, parallel command recording, and split GPU
  queues until a project proves the pressure.

This gives Cubey the structural benefits of multithreading early while keeping
the first implementation understandable and testable.

## Vulkan Constraints

Vulkan differs from OpenGL in a useful way: there is no GL-style context that
forces one context-owning thread. The application is responsible for host-side
threading and external synchronization.

The constraints that matter for Cubey:

- Multithreading mostly improves CPU-side work. Vulkan calls that interact with
  shared device objects still need correct synchronization.
- A `VkCommandPool` must not be used concurrently from multiple host threads.
  Parallel command recording requires per-thread command pools, and usually
  per-frame/per-thread resource pools.
- Descriptor pools have similar external-synchronization concerns when
  allocating, freeing, resetting, or updating descriptor sets.
- A single `VkQueue` can only be submitted to from one host thread at a time.
  Multiple queues are unordered relative to each other unless synchronized with
  semaphores.
- A single universal graphics/compute/present queue remains reasonable until a
  project benefits from split graphics, compute, or transfer queues.

References:

- Vulkan threading guide:
  https://docs.vulkan.org/guide/latest/threading.html
- Vulkan command-buffer multithreading sample:
  https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html
- Vulkan queues guide:
  https://github.khronos.org/Vulkan-Site/guide/latest/queues.html

## Goals

- Prevent app/project code from directly blocking on GPU readback, image
  encoding, or setup-time upload paths when a queued shape would be easy.
- Keep GPU ownership and queue submission explicit enough that validation
  errors and synchronization bugs remain debuggable.
- Make worker-thread work useful early for image encoding, asset decoding,
  file IO preparation, shader/pipeline preparation, procedural CPU data, and
  future project simulation setup.
- Keep the third-party task dependency hidden behind Cubey APIs.
- Provide a deterministic or inline executor for tests so the async shape does
  not make unit tests flaky.

## Non-Goals

- Do not build a general game-engine job system.
- Do not expose Taskflow, `BS::thread_pool`, oneTBB, HPX, `stdexec`, or another
  third-party API through public Cubey headers.
- Do not move GLFW or all project loops into `cubey::vulkan`.
- Do not add a dedicated render thread before there is a host layer that needs
  it.
- Do not add parallel command recording before command recording time is
  measurable enough to justify the added pools and synchronization.
- Do not split graphics, compute, present, or transfer queues before a project
  proves the benefit.

## Threading Vocabulary

These are conceptual roles, not all required to be separate OS threads on day
one.

- **App thread:** owns window events, user input, high-level project state, and
  the main loop. Today this is the example loop.
- **GPU owner:** owns Vulkan object mutation, queue submission, in-flight frame
  tracking, and deferred destruction. Initially this can run on the app thread.
  Later it may become a dedicated render/submission thread.
- **Worker executor:** runs CPU-only jobs. Worker jobs must not mutate Vulkan
  objects or call Vulkan submission APIs unless a future API explicitly grants a
  thread-local recording context.
- **Frame packet:** immutable or mostly immutable render intent produced by app
  or project code for the GPU owner. This should contain resource handles,
  draw/dispatch intent, constants, and capture requests, not raw ownership of
  mutable GPU objects.
- **Upload request:** CPU data plus desired GPU resource usage, queued for the
  GPU owner to stage, copy, transition, and publish.
- **Capture request:** a request to copy GPU output into a readback buffer,
  later poll completion, then optionally encode on a worker.
- **Frame ticket:** a monotonically increasing frame or submission identifier
  used for deferred destruction and readback readiness.

## Ownership Model

Public Cubey objects should be move-only unless there is a clear reason to
share ownership. They are thread-compatible by default, not thread-safe: callers
may move or use them from one thread at a time, and shared concurrent mutation
is invalid unless an API says otherwise.

Vulkan wrapper policy:

- `Device` owns device and queue handles, but submission should flow through the
  GPU owner rather than arbitrary worker threads.
- `CommandPool` remains single-thread-owned. Future parallel recording should
  allocate command pools by frame and worker index.
- `DescriptorPool` remains single-owner unless a future allocator explicitly
  shards pools per frame and worker.
- Resource destruction should move toward deferred destruction once multiple
  frames or async requests can keep GPU work alive beyond the immediate call.
- Readback data should be copied into owned CPU buffers before worker-side
  encoding or comparison.

## Data Flow

The intended project frame shape:

```text
poll window/input
project update
submit CPU jobs or consume completed job results
build frame packet
GPU owner:
  begin frame
  drain ready upload requests
  record commands
  enqueue readback/capture copies
  submit/present
  retire completed frame tickets
workers:
  decode assets
  generate CPU data
  encode completed captures
```

The first implementation can execute all GPU-owner work synchronously on the
app thread. The important design constraint is that project code talks to
queues, packets, and handles instead of calling every blocking path directly.

## Job Layer

Add `cubey::jobs` as a small facade over whichever executor Cubey chooses.
Public code should depend on Cubey vocabulary:

- `JobSystem` or `Executor`
- `JobHandle<T>` or future-like result wrapper
- `submit`
- `submit_range` or `parallel_for`, only if a concrete project needs it
- deterministic inline executor for tests
- clean shutdown that waits for accepted work and rejects new work

Candidate dependencies:

- **Taskflow:** best fit if Cubey wants dependency-shaped frame work, task
  graphs, composition, and profiling. It is header-only and supports task graph
  dependencies. Survey reference: https://github.com/taskflow/taskflow
- **BS::thread_pool:** best lightweight fit if Cubey only needs a durable CPU
  pool with futures, optional priorities, and minimal integration cost. It is
  single-header, standard-library-only, and MIT licensed. Survey reference:
  https://github.com/bshoshany/thread-pool
- **oneTBB:** mature and powerful, but heavier than Cubey currently needs.
- **stdexec:** directionally important for C++26, but too early to anchor this
  project.
- **HPX:** too broad for Cubey's current scope.

Initial implementation: `cubey::jobs::JobSystem` uses a minimal
standard-library worker pool behind the Cubey facade, paired with
`InlineExecutor` for deterministic tests. Taskflow and `BS::thread_pool` remain
future swap candidates if dependency graphs or richer pool features become
worth the dependency. Either way, do not expose the dependency in public
project-facing APIs.

## Uploads, Captures, And Readbacks

Uploads should become queued requests even before they are truly asynchronous.
This lets project code say what data it needs on the GPU without dictating when
the staging copy and layout transitions happen.

Capture/readback should avoid this shape as the default:

```text
record copy -> submit -> wait idle -> map -> encode PNG
```

Preferred shape:

```text
request capture -> record copy in frame N -> fence/ticket completes later
-> map/download -> encode PNG on worker -> publish result
```

The headless path may still block at process shutdown to produce its required
artifact, but the runtime should make the blocking wait visible and keep the
interactive path non-stalling.

Initial implementation: `cubey::CaptureQueue` accepts completed RGBA8 pixel
buffers and schedules PNG encoding through `cubey::jobs`, returning a
`CaptureTicket` whose `finish()` call makes the blocking wait explicit. GPU
readback is still direct in current examples; later slices should move the GPU
copy/poll side behind the same request/ticket vocabulary.

`cubey::UploadQueue` is the first upload-side request queue. It owns submitted
CPU bytes, returns monotonic `UploadTicket` values, and lets the future GPU
owner drain queued uploads in submission order. The queue is guarded internally
so worker jobs can safely submit CPU-side upload requests before any Vulkan
staging/copy work is introduced.

`cubey::FrameTicketIssuer` and `DeferredDestructionQueue` are the first
CPU-side lifetime primitives. They do not yet query Vulkan fences; they provide
the monotonic ticket and retire-after-ticket vocabulary that later
N-frames-in-flight and readback polling can connect to real GPU completion.

## Command Recording

Keep current primary-command-buffer recording until command recording time is a
measured bottleneck. When needed, introduce parallel recording in this order:

1. Frame/thread command-pool sharding.
2. A thread-local recording context that exposes only safe per-thread Vulkan
   state.
3. Secondary command buffers for repeated draw-list chunks.
4. Primary submission assembly on the GPU owner.

Do not let arbitrary workers record commands against shared command pools or
descriptor pools. That would create the worst kind of Vulkan bug: rare, timing
dependent, and hard to validate.

## Queue Model

Cubey currently selects one queue family that supports graphics, compute, and
present. Keep that as the default until a project benefits from split queues.

Future queue work should proceed in this order:

1. Represent queue families explicitly in `Device`.
2. Centralize queue submission through a submission coordinator.
3. Add timeline-semaphore or fence-backed frame tickets.
4. Add transfer/compute queues only when an upload-heavy or compute-heavy
   project has evidence that a separate queue helps.

The runtime should never imply that separate `VkQueue` handles automatically
mean real hardware concurrency. Vulkan leaves that mapping implementation
defined.

## Runtime Boundary

Before the first real project gets large, introduce an async-ready project
boundary. The exact names can change, but the shape should be:

```cpp
struct ProjectFrame {
    double delta_seconds;
    double elapsed_seconds;
    std::uint64_t frame_index;
    FrameTicket ticket;
};

struct RenderPacket {
    // Resource handles, draw/dispatch intent, constants, capture requests.
};

class Project {
public:
    void setup(ProjectContext& context);
    void update(ProjectFrame frame, ProjectContext& context);
    RenderPacket render_packet(ProjectFrame frame, ProjectContext& context);
    void resize(VkExtent2D extent);
    void shutdown(ProjectContext& context);
};
```

`ProjectContext` should expose stable Cubey services: jobs, uploads, capture
requests, timing, and eventually UI hooks. It should not expose raw queue
submission as a casual escape hatch.

Examples can stay explicit. Projects should use this boundary once it exists.

Initial implementation: `ProjectContext` exposes jobs, uploads, captures, frame
tickets, and deferred destruction as services. `ProjectRuntimeServices` owns
those services and issues `ProjectFrame` values from `FrameTiming`. `ProjectFrame`,
`ProjectExtent`, `RenderPacket`, and the `ProjectLike` concept define the first
compile-time checked lifecycle shape for future `projects/` code. `fluid_2d`
now consumes `ProjectFrame` for simulation timing, but examples remain direct.

## Error Handling And Shutdown

- Worker exceptions must be captured and returned through job handles or an
  explicit error channel.
- Shutdown should stop accepting new work, wait for accepted CPU jobs, drain or
  cancel pending upload/capture requests, wait for required GPU work, then
  destroy resources.
- Interactive capture failures should be reported without crashing the process
  unless the caller requested strict output.
- Headless required-output mode should fail with a nonzero exit code if capture
  or encoding fails.

## Testing

Near-term test strategy:

- Unit-test the `cubey::jobs` facade with an inline executor and the real
  executor.
- Test worker exception propagation.
- Test shutdown behavior: accepted jobs complete, new jobs are rejected after
  shutdown starts.
- Keep current CTest window/no-display/headless coverage.
- Add a thread-sanitizer preset once the first real concurrent code lands.
- Add a fake upload/capture queue test before threading it into Vulkan.

Runtime smoke strategy:

- Use validation layers for all Vulkan threading slices.
- Add headless artifact tests for capture paths.
- Avoid performance claims until release builds without validation layers are
  measured.

## Implementation Slices

### Slice 0: Documentation

Status: this document.

Capture the model before implementation so the first project does not grow
around blocking setup/readback calls.

### Slice 1: Job Facade Spike

Status: initial pass complete.

- Added `JobSystem`, `JobHandle`, and `InlineExecutor` under `cubey::jobs`.
- Kept the implementation standard-library-only for now while preserving the
  facade needed to swap in Taskflow or `BS::thread_pool` later.
- Added tests for completion, exception propagation, shutdown, and deterministic
  inline execution.

### Slice 2: Async-Ready Upload And Capture Queues

Status: initial pass complete.

- Added a CPU-side PNG capture queue for already-completed RGBA8 pixels.
- Kept GPU readback unchanged while making PNG encoding job-backed and
  ticket-based.
- Blocking waits now happen through explicit `CaptureTicket::finish()`.
- Added a CPU-side upload request queue that owns submitted bytes until the GPU
  owner drains them.

### Slice 3: First Project Runtime Boundary

Status: initial pass complete.

- Added a small project runtime vocabulary for `projects/`, not existing
  examples.
- Added `ProjectContext` service access to jobs, uploads, captures, frame
  tickets, and deferred destruction.
- Added `ProjectRuntimeServices` as the first project-owned service bundle and
  frame-ticket issuer.
- Added a `ProjectLike` concept for setup/update/render-packet/resize/shutdown
  lifecycle checks.
- The first real project now consumes `ProjectFrame` for timing; frame packets
  and queued upload/capture requests remain future pressure.

### Slice 4: Frame Overlap And Deferred Destruction

Status: frame-ticket/deferred-destruction initial pass complete.

- Added frame ticket issuance and comparison.
- Added deferred destruction actions retired by completed ticket.
- Kept this CPU-side; Vulkan fence/timeline integration remains future work.

### Slice 5: Parallel Command Recording And Split Queues

- Add only after profiling or project complexity proves the need.
- Start with per-thread command pools and secondary command buffers.
- Split queue families and timeline semaphores only after a concrete project
  demonstrates benefit.

## Open Questions

- Should Cubey prefer Taskflow first for graph-shaped frame work, or
  `BS::thread_pool` first for the smallest useful CPU job layer?
- Should the current standard-library `cubey::jobs` implementation stay long
  term, or should a future workload justify swapping in Taskflow or
  `BS::thread_pool` behind the same API?
- Should `ProjectRuntimeServices` grow into a project adapter, or should the
  next project keep using the windowed/headless hosts directly?
- Should GPU capture polling or queued uploads become the first real
  Vulkan-backed async path?
