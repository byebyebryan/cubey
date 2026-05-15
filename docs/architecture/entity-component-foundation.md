# Entity And Component Foundation

This document captures Cubey's intended entity/component foundation before
scene, renderable, camera, light, material, or asset ownership grows around
ad hoc APIs. The goal is to establish the shape and threading contract first,
then implement narrow slices against it.

## Direction

Cubey should use a manager-oriented ECS-lite design:

- `Engine` is the scoped root owner for engine services and scene creation.
- `Entity` is a small generational identity handle with no behavior.
- `EntityManager` owns entity creation, destruction, liveness, and generation
  validation.
- `RenderResourceRegistry` owns CPU-side mesh/material handle identity and
  liveness labels for scene renderables, without owning Vulkan resources.
- Type-specific component managers own storage and APIs for durable rendering
  concepts: transform, renderable, camera, light, and bounds.
- `Scene` owns the entity manager, component managers, edit commits, and read
  view publication.
- `TransformManager2D` and `TransformManager3D` are the first entity-backed
  component managers and own parented local/world transform data.
- `CameraManager2D`, `CameraManager3D`, `RenderableManager3D`, and
  `LightManager3D` build on the same stable read-view/edit-queue pattern for
  single-instance scene components.

This borrows Filament's public architecture shape more than its internals:
shared entity IDs, type-specific managers, component instances, parented
transforms, and manager-owned local/world data. Cubey should not import a broad
generic registry, archetype ECS, reflection system, or heavily metaprogrammed
component manager as the public foundation.

## Core Goals

- Make threading and data stability first-class from the first implementation.
- Keep component APIs readable and explicit, even when their storage is shared.
- Use established renderer terminology: entity, component, manager, instance,
  scene, transform, renderable, camera, light.
- Keep `Transform2D` and `Transform3D` as plain local affine value types.
  `Transform3D` may carry an explicit affine matrix override when imported data
  cannot be represented as TRS without losing authoring intent.
- Let managers expose high-level operations without exposing their storage
  mechanics.
- Support efficient iteration and cache-aware storage without invalidating
  readers through swap-remove compaction.

## Non-Goals

- A generic ECS query language.
- Archetype/chunk migration.
- Runtime reflection or script-facing component metadata.
- Scene-node behavior, update callbacks, or game-object inheritance.
- Lock-free mutation as a baseline requirement.
- Fully compact storage when it conflicts with reader stability.
- Serialization, prefab, editor, or asset-database policy.

## Vocabulary

`Entity`:
An opaque generational handle. It identifies an object that can own zero or
more components. It does not store a transform, parent, name, or behavior.

`Instance`:
A manager-local typed handle for a component slot, such as
`TransformInstance3D`. Instances are useful for hot manager access and
iteration. Public APIs may also accept `Entity` when clarity is better than
avoiding a lookup.

`Component manager`:
A type-specific manager that owns one component type. It maps entities to
instances, validates handles, owns stable component storage, and exposes
domain-specific APIs.

`Scene`:
The owner and synchronization boundary for entity/component managers. It
coordinates edit commits, read-view publication, component cleanup, and
cross-manager destruction.

`Engine`:
The non-singleton root owner for engine-wide services and `Scene` instances.
It creates and destroys scenes, owns render resource handle identity, exposes
project runtime contexts, and gives future engine-wide managers a stable home
without making access global.

`SceneEditQueue`:
A thread-friendly list of requested structural changes and component writes.
Worker jobs can build edit queues without directly mutating component storage.

`SceneReadView`:
A consistent, read-only view of a committed scene epoch. Render packet
building, culling, and other read-mostly jobs should consume read views rather
than mutating managers.

## Threading Contract

Managers are thread-compatible, not freely thread-safe mutable containers.
Cubey guarantees structural stability for published read views and explicit
edit/commit phases.

Rules:

- Read access happens through `SceneReadView` or manager read views.
- Structural mutation happens through an exclusive transaction or queued edits
  applied by `Scene::commit`.
- Component storage is not compacted, moved, or reused while any active read
  view can observe it.
- Destroyed entities and components become logically dead at commit, but their
  storage is retired only after no older read view can reference it.
- Read-view validation is epoch-local. A read view acquired before a later
  destroy commit can still read the component snapshot it published with, while
  latest-scene mutation APIs reject the stale handle after the destroy commit.
- Mutating the same component from multiple threads still requires explicit
  synchronization or serialized edits. Cubey prevents unrelated structural
  changes from invalidating readers; it does not make same-slot writes
  magically safe.
- Read views do not perform lazy writes. World transform updates, dirty-list
  processing, and cleanup happen before publishing the view or during an
  exclusive update phase.

The intended lifecycle:

```text
app/update thread:
  collect input and project state
  build local edits

workers:
  build CPU-only edits or data products

scene owner:
  merge edit queues
  validate entity and component handles
  apply structural changes to stable slots
  update transform world matrices and other dirty data
  publish a new read view epoch
  retire slots whose epoch is older than every active read view

render/build workers:
  acquire SceneReadView
  build render packets from stable read-only data
  release SceneReadView
```

## Handle Model

Use generations at every handle boundary that can outlive deletion.

`Entity` should contain:

- slot index;
- generation;
- a null value.

`Instance` should contain:

- manager-local slot index;
- component generation;
- a null value.

Validation checks both index bounds and generation. Destroying an entity or
component increments the relevant generation at commit, so stale handles stop
matching after logical destruction. Slot memory is not immediately reused if an
older read view could still observe it.

The exact bit layout can be chosen during implementation. The contract matters
more than the packing: stale handles must be detectable, copied handles must be
cheap, and null handles must be representable.

When a worker edit queue needs to create an entity and attach components to it
before commit, entity creation must reserve a handle up front:

```cpp
SceneEditQueue edits;
Entity entity = edits.create_entity();
edits.transforms3d().create(entity, Transform3D{});
```

That reserved entity is valid inside the edit queue immediately, but it becomes
visible to scene read views only after commit. If commit fails, the reserved
entity is destroyed as part of rolling back the rejected edit batch.

## Storage Model

Component managers should store component data in stable paged slots:

```cpp
struct TransformPage3D {
    Entity entities[PageSize];
    std::uint32_t generations[PageSize];
    SlotState states[PageSize];
    Transform3D local[PageSize];
    math::Mat4 world[PageSize];
    ParentLink parents[PageSize];
    ChildLinks children[PageSize];
    DirtyState dirty[PageSize];
};
```

Pages are heap allocations owned by the manager. Adding a page does not move
existing slots. Destroying a component marks a slot pending-dead; it does not
swap another component into that slot. Reuse happens through a free list after
the slot's retire epoch is safe.

Iteration should not require moving component storage. Managers can maintain
separate active-instance snapshots:

- the write side updates manager storage during commit;
- commit builds or updates an immutable active-instance list for the new epoch;
- `SceneReadView` holds the active list for its epoch;
- later commits publish new lists without mutating lists held by older views.

This is less compact than a packed dense vector, but it makes MT behavior
predictable. When a renderer needs denser data, build compact frame packets or
GPU upload buffers from the read view rather than changing the manager's stable
storage contract.

Simple one-component-per-entity managers can share internal stable-slot,
entity lookup, snapshot publication, destroy, and retire machinery. The shared
helper is an implementation detail; public managers remain type-specific and
domain-shaped. Transform managers intentionally keep custom storage because
parent/child links, dirty propagation, and local/world affine cache updates are
part of the transform domain.

Current implementation note: the transform manager public header owns the
contract, type aliases, read-view shape, and explicit 2D/3D instantiation
declarations. The template member definitions live in `src/cubey/scene` and are
explicitly instantiated for `Transform2D` and `Transform3D`, keeping parented
hierarchy implementation detail out of the main public include surface.

## Render Resource Handles

`MeshHandle` and `MaterialHandle` are CPU-side typed IDs carried by renderable
components and render packets. `RenderResourceRegistry` issues, validates, and
destroys those handles with generations, mesh labels, and material tags. It
does not own Vulkan meshes, textures, descriptors, materials, or pipelines.

Scenes created through `Engine` receive the registry and reject renderable
creates/updates that reference destroyed or stale mesh/material handles. A bare
`Scene` remains usable for focused tests and simple code; without a registry it
only enforces non-null handles and primitive validity.

Concrete resource objects stay outside the registry. Examples/projects can map
handles to their own `Mesh` or future material resources through
`ResourceTable`, and CPU draw planning can attach registry metadata to
renderable packets before a project records Vulkan commands.

## Mutation Model

Cubey should support two mutation paths:

```cpp
SceneEditQueue edits;
Entity entity = edits.create_entity();
edits.transforms3d().create(entity, Transform3D{}, parent);
edits.transforms3d().set_local_transform(entity, local);
edits.transforms3d().set_parent(child, parent);

scene.commit(edits);
```

Destroy edits use the same queue shape:

```cpp
SceneEditQueue edits;
edits.destroy(entity);
scene.commit(edits);
```

and:

```cpp
SceneTransaction txn = scene.begin_transaction();
Entity entity = txn.entities().create();
txn.transforms3d().create(entity, Transform3D{});
txn.commit();
```

`SceneEditQueue` is the worker-friendly path. `SceneTransaction` is the
exclusive owner path for setup, tests, and simple applications. Both publish
through the same commit machinery.

Commit should be responsible for:

- validating entity liveness and component existence;
- publishing reserved entities from edit queues;
- rolling back reserved entities from failed edit queues;
- detecting invalid parent links and transform cycles;
- applying creates, destroys, reparents, and local transform writes;
- invalidating stale handles through generation updates;
- updating dirty transform world matrices;
- publishing a read epoch.

## Transform Manager Shape

`Transform2D` and `Transform3D` remain local affine values. `Transform3D` can
also carry an explicit affine matrix override for imported static transforms;
sampled animation writes normal TRS values again. Parenting belongs to transform
managers:

```cpp
Entity parent = scene.entities().create();
Entity child = scene.entities().create();

auto txn = scene.begin_transaction();
txn.transforms3d().create(parent, Transform3D{});
txn.transforms3d().create(child, Transform3D{}, parent);
txn.commit();

SceneReadView view = scene.read();
TransformInstance3D child_transform = view.transforms3d().instance(child);
math::Mat4 world = view.transforms3d().world_affine_matrix(child_transform);
```

Design rules:

- Creation attaches one transform component to one entity.
- A transform component can have zero or one parent transform.
- Parent links are validated at commit.
- Destroying a transform with children should follow an explicit policy. The
  first policy should be strict: reject destroying a transform that still has
  children unless the edit also reparents or destroys those children.
- Read views expose local and world transforms without mutating the manager.
- Bulk local transform changes are batched and world matrices are updated
  before publishing the read view.

## Manager Pattern

Each manager should be explicit:

```cpp
class TransformManager3D;
class RenderableManager3D;
class CameraManager2D;
class CameraManager3D;
class LightManager3D;
```

Managers may share internal utilities such as `StableSlotStore`, but public
component APIs should stay domain-specific. This avoids a large template-heavy
public ECS while still giving Cubey a repeatable manager pattern.

The shared internal utility should cover:

- paged slot allocation;
- slot generation;
- free-list and retire-list handling;
- active instance snapshot publication;
- entity-to-instance lookup;
- validation helpers.

The shared utility should not decide transform parenting, renderable material
binding, camera projection policy, or light behavior.

## Relationship To Filament

Filament's architecture is the closest precedent for Cubey's renderer-oriented
shape:

- `Entity` is a compact generational handle.
- `EntityManager` owns thread-safe creation, destruction, generations,
  free-list reuse, and liveness checks.
- Component managers map `Entity` to typed `Instance` values.
- `SingleInstanceComponentManager` stores components as structure-of-arrays.
- `TransformManager` owns transform components, parent links, local/world
  transforms, and a local transform transaction API.

Cubey should borrow:

- shared entity identity;
- type-specific managers;
- manager-local instance handles;
- SoA-aware storage;
- explicit transform transactions;
- entity destruction notifications to component managers.

Cubey should avoid:

- a global singleton entity manager;
- public dependence on a large generic component-manager template;
- swap-remove component deletion as the default storage policy;
- raw index instances that shift when unrelated components are removed;
- random/incremental component GC as the first cleanup model;
- implementation cleverness that makes ownership and MT behavior hard to read.

References:

- [Filament TransformManager](https://github.com/google/filament/blob/main/filament/include/filament/TransformManager.h)
- [Filament Entity](https://github.com/google/filament/blob/main/libs/utils/include/utils/Entity.h)
- [Filament EntityManager](https://github.com/google/filament/blob/main/libs/utils/include/utils/EntityManager.h)
- [Filament EntityManagerImpl](https://github.com/google/filament/blob/main/libs/utils/src/EntityManagerImpl.h)
- [Filament SingleInstanceComponentManager](https://github.com/google/filament/blob/main/libs/utils/include/utils/SingleInstanceComponentManager.h)
- [Filament EntityInstance](https://github.com/google/filament/blob/main/libs/utils/include/utils/EntityInstance.h)

## Relationship To Current Cubey Code

The initial entity/component substrate now exists: `EntityManager`, stable slot
storage, `Scene` edit/read epochs, entity-backed 2D/3D transform managers,
entity-backed 2D/3D camera managers, the first 3D renderable manager, and the
first 3D light manager. The older transform-only hierarchy has been retired in
favor of the manager shape described here.

`Engine` now provides the first Filament-style root ownership boundary, but it
does not own renderer/device setup yet. Windowed and headless hosts remain the
GPU/platform owners until Cubey defines a higher-level renderer ownership
contract.

`cubey::render` should continue to own low-level renderer-facing resources,
opaque resource handles, and draw metadata. It should not become the scene
owner. Scene/component managers can build renderable packets that reference
`cubey::render` resource handles plus light packets that carry CPU-side light
data, and the GPU owner resolves or interprets those packets during command
recording.

The existing threading direction still applies: one GPU owner serializes
Vulkan mutation and submission, while worker threads prepare CPU-side data and
read committed scene views. Scene read views should feed render-packet creation
without exposing mutable Vulkan objects to workers.

## Error Handling

Public debug/development APIs should fail loudly on invalid structural edits:

- creating a component for a dead entity;
- creating a duplicate single-instance component;
- using a stale entity or instance generation;
- parenting a transform to itself;
- creating a transform cycle;
- destroying a transform that still has children without an explicit child
  policy in the same edit batch.
- creating or updating a renderable with no primitives, null mesh/material
  handles, or zero draw instances.
- creating or updating a light with a zero directional vector, negative
  color/intensity, or a non-positive point-light range.

The first implementation can use exceptions consistently with current Cubey
tests. Later hot paths can add no-throw validation or result-code variants if a
project needs them.

## Testing Strategy

Unit tests should cover:

- entity creation, destruction, liveness, generation invalidation, and reuse;
- component create/destroy and stale instance rejection;
- no swap-remove movement when unrelated components are destroyed;
- read view stability across a later commit;
- deferred slot reuse while an older read view exists;
- transform parent/child world matrix updates during commit;
- invalid parenting, self-parenting, and cycle rejection;
- strict child policy on transform destruction;
- renderable packet extraction from committed transforms;
- light packet extraction from committed lights and transforms;
- concurrent edit-queue construction feeding a serialized commit;
- thread sanitizer coverage once real concurrent access lands.

Integration tests should prove:

- a renderable packet can be built from a `SceneReadView`;
- a light packet can be built from a `SceneReadView`;
- transform managers cover parented world-transform behavior previously covered
  by standalone hierarchy tests;
- destroying entities retires attached components through the scene boundary;
- old read views remain valid while newer commits publish updated data.

## Deep Design Review

### Strengths

The design gives Cubey a renderer-shaped foundation without adopting a full
generic ECS. It keeps public vocabulary close to Filament and other established
engines while making storage and synchronization simpler to reason about.

The MT contract is explicit: readers use snapshots, writers commit edits, and
storage is not compacted underneath active readers. This avoids the common
retrofit problem where a dense packed manager later needs to support jobs,
culling, render packet generation, and destruction across frame boundaries.

Stable paged slots make handle validation and reader safety straightforward.
They also leave room for SoA page layout, dirty lists, and compact frame-packet
generation without making all manager storage compact all the time.

### Risks

Stable slots trade some iteration locality for safety. Cubey should recover
iteration performance by building compact render packets or per-frame upload
buffers from read views, not by weakening the manager storage contract.

Read-view retention can delay memory reuse. The first implementation should
track active read epochs and expose diagnostics for pending-retire counts so
long-lived views are visible during tests.

Epoch-local validation adds implementation cost. Read views need enough
snapshot metadata to distinguish "valid in this view" from "valid in the
latest mutable scene." This is necessary for concurrent readers, but the
metadata should stay narrow: active instance lists, slot/generation snapshots,
and an epoch token.

Queued edits need deterministic conflict rules. If two edit queues write the
same component in one commit, Cubey should define commit order explicitly
rather than racing or merging implicitly. The first rule should be "edit queues
are applied in caller-provided order; later writes win for simple value
updates; conflicting structural edits fail loudly."

Entity reservation from worker edit queues needs all-or-nothing commit
semantics. A failed commit must not leak a published entity, and a successful
commit must publish the entity and all attached components in one epoch.

Transform parenting is cross-slot graph state. Cycle detection must consider
the post-commit graph, not only the pre-commit graph, because multiple reparents
can be submitted together.

Entity destruction touches every manager. `Scene` should coordinate this
instead of letting managers observe global destruction in an arbitrary order.

### Review Outcome

This design is appropriate for Cubey if we accept one deliberate tradeoff:
manager storage prioritizes MT stability and correctness over always-dense
component arrays. That tradeoff matches the project's direction because Cubey
already treats frame packets, uploads, captures, and GPU ownership as explicit
boundaries.

The smallest useful substrate is now in place:

1. `Entity` and `EntityManager`.
2. Internal stable paged slot store.
3. `Scene` transaction/read-view epoch mechanics.
4. Shared transform-manager template used by 2D/3D.
5. Migration of transform hierarchy behavior to the manager shape.

Camera, renderable, and light managers now build on this contract. Material,
bounds/culling, environment, and resource-registry-adjacent managers should
follow the same pattern instead of reintroducing project-local ownership
patterns.
