# KairoECS — why a component is not an entity and an entity is not an ID on disk

This guide connects [`Entity.cppm`](../Entity.cppm),
[`SparseSet.cppm`](../SparseSet.cppm) and
[`Registry.cppm`](../Registry.cppm) to the
[KairoGameEngine scene/ECS bridge](https://github.com/swayam8624/KairoGameEngine/blob/docs/teaching-walkthrough-20261121/Runtime/KairoSceneECSBridge.cppm).

## 1. Three different identities

An **authored EngineCore Entity** names an object in a saved project scene.
A **runtime ECS Entity** contains an index and generation into an in-memory
Registry. A **renderer ObjectID** identifies a draw instance for picking.
They need not be numerically equal, and must never be interchanged.

The ECS generation exists because storage slots can be reused. If an
entity in slot 4 is destroyed and slot 4 is allocated again, a stale
`Entity{4, oldGeneration}` should fail `Registry::IsAlive` rather than
access the new owner's components.

`Entity::IsValid` only excludes the sentinel `InvalidIndex`. It is not
a liveness test. `Registry::IsAlive` also checks vector bounds, the
alive bitmap and the exact generation. Destroy advances generation
unless it would overflow, in which case that slot is retired.

## 2. Dense/sparse storage

For component type `Position`, `SparseSet<Position>` maintains:

```text
Sparse[entity.Index]    -> dense position, or Missing
DenseEntities[dense]    -> full (index, generation) Entity
DenseComponents[dense]  -> Position value
```

Lookup first tests the sparse index, then confirms the full generational
handle at the resulting dense slot. Insertion constructs the entity and
component in contiguous arrays before publishing its sparse map entry.

Removal uses *swap with last* to preserve dense storage without an
O(n) gap shift. Thus component iteration is cache-friendly but dense
ordering is not stable. Removing a component can invalidate pointers
to the removed and moved elements; growing the vectors may invalidate
all previously returned references.

**Source-review caveat:** `SparseSet::Remove` is marked `noexcept` and
move-assigns `Component` during compaction. A component type whose move
assignment throws can cause termination. This deserves an explicit type
constraint or exception-safety redesign and regression test in a separate
functional pass.

## 3. Registry owns entity/component lifetimes

`Registry::Create` reserves a structural journal entry and allocates a
live slot. `Registry::Destroy` removes that entity from every component
pool, records destruction and makes the old handle stale. `Add` and
`Remove<Component>` record structural changes.

The journal stores event kind, subject and a sequence number; it does
not automatically contain enough serialized component values to
reconstruct a saved scene. It is observability, not authoring undo.

## 4. Component joins and mutation boundaries

A system can call `Registry::Each<Position, Velocity>` to iterate
entities possessing both components. The implementation scans the
*smaller* dense pool and resolves the second via the sparse table.
Three-component joins follow a similar smallest-pool strategy.

There is a precondition: **do not mutate the primary component storage
or destroy the entities being iterated inside the callback**. The dense
vector can reorder, and the stored traversal count/references become
invalid. Queue structural changes and apply them after iteration,
using EngineCore RuntimeWorld or another explicit command boundary.

```cpp
// Illustrative pseudocode: actual component imports/types depend on
// your runtime module. Defer destruction until the join finishes.
std::vector<kairo::ecs::Entity> toDestroy;

registry.Each<Position, Velocity>(
    [&](kairo::ecs::Entity e, Position& position, Velocity& velocity) {
        position.x += velocity.x * dt;
        if (position.x > limit) toDestroy.push_back(e);
    });

for (const auto e : toDestroy) registry.Destroy(e);
```

The example's important guarantee is structural-mutation separation,
not that an ECS update automatically advances PhysicsWorld.

## 5. Source-to-runtime conversion

KairoGameEngine's `SceneECSMirror::Extract` creates all runtime ECS
entities from authored EngineCore Scene records in one pass and then
resolves hierarchy references in a second pass. An
`AuthoredEntityIdentity` component retains the durable scene entity
ID. If the scene is replaced or changed, the host must explicitly
rebuild/reconcile the mirror; the conversion is not bidirectional.

A GPU mesh handle and a physics BodyID belong to still different
resource owners. ECS storage doesn't implicitly create either one.

## 6. Tests and performance

Read [`tests/ECSTests.cpp`](../tests/ECSTests.cpp) and
[`benchmarks/ECSBenchmark.cpp`](../benchmarks/ECSBenchmark.cpp).
A useful test suite creates/destroys/reuses slots, tests stale
generation access, attaches/removes components, checks two/three-pool
joins, and tests references invalidated by swap-remove.

For benchmarking, distinguish total registry entities, component
density, cache layout, join selectivity, random access and structural
change frequency. “O(1) lookup” is not a universal fastest-runtime
claim; data layout, allocator behavior and workload matter.

**Evidence status:** source-inspection guide. No compiler or benchmark
was run as part of this teaching edit.
