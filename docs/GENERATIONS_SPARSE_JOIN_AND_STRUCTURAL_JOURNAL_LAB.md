# ECS laboratory: generational identity, sparse-set mutation and smallest-pool joins

Follow this laboratory beside [Entity.cppm](../Entity.cppm), [SparseSet.cppm](../SparseSet.cppm), [Registry.cppm](../Registry.cppm) and [ECSTests.cpp](../tests/ECSTests.cpp). The existing [ECS architecture walkthrough](ECS_WALKTHROUGH.md) introduces the vocabulary; this lesson derives the actual storage and mutation invariants, then traces real failure cases. The consuming [KairoGameEngine SceneECSMirror](https://github.com/swayam8624/KairoGameEngine/blob/main/Runtime/KairoSceneECSBridge.cppm) connects these transient handles to persistent EngineCore entities.

## 1. Three identities for one visible game object

A saved game scene uses a persistent **EngineCore Entity** with a stable integer ID. When it is extracted into ECS, Registry::Create allocates a process-local pair **(Index, Generation)**. The renderer subsequently uses its own uploaded MeshHandle, while PhysicsWorld uses a BodyID.

These IDs are not interchangeable. An authored entity number does not identify an array slot in ECS; a recycled ECS index does not uniquely identify a previously live object. Only the complete (Index, Generation) pair guards stale references. SceneECSMirror stores AuthoredEntityIdentity as an actual ECS component so an ECS handle can be traced back to the stable authored ID.

A bare Entity::IsValid only checks whether Index equals its reserved sentinel. Registry::IsAlive also checks bounds, active state **and the generation** currently stored for that slot.

## 2. Why generations prevent the ABA problem

Imagine the registry creates entity A with (index 0, generation 1). Some gameplay system keeps its handle. A is destroyed. The registry removes every component, marks slot 0 dead and increments generation to 2 before adding slot 0 to the free list. The next Create may return B=(0,2).

If a system compared only Index, it would think A and B were the same entity. Registry::IsAlive(A) must now return false; IsAlive(B) must return true. The same rule is checked again when a SparseSet::Contains looks up components, by comparing the **entire** Entity stored at that dense location.

If the generation reaches uint32 maximum, Registry deliberately *retires* that slot instead of wrapping to generation 1 and risking an ancient handle becoming valid again.

**Exercise:** retain an old Entity handle through Destroy and Create, then assert that the index may repeat, the generation does not, and Get/TryGet never exposes B's Position through A.

## 3. Sparse and dense storage are two views of the same allocation

A SparseSet<Component> stores three vectors:

- **m_Sparse[entity.Index] → dense index** or Missing sentinel;
- **m_Entities[dense] → complete Entity** for generation validation;
- **m_Components[dense] → Component** next to other components for efficient iteration.

The sparse array's length follows the maximum entity index encountered; dense arrays store only entities carrying this component. Membership lookup takes bounded table accesses, and dense iteration accesses consecutive component values instead of chasing one heap object per entity.

For example, if entity slots 1, 4 and 9 have Position, dense arrays might contain:

| Dense position | Entity slot | Component |
| --- | --- | --- |
| 0 | 1 | Position A |
| 1 | 4 | Position B |
| 2 | 9 | Position C |

The sparse entries at 1, 4, 9 are respectively 0, 1, 2; absent indexes contain Missing.

**Exercise:** construct this state, then remove entity-slot 4's Position. Which dense entry moves, and which m_Sparse mapping changes?

## 4. Derive swap-removal

Removing Position from entity 4 removes dense index 1. The last dense record at index 2 (entity 9) moves to index 1. The vectors then pop their last elements, m_Sparse[9] becomes 1 and m_Sparse[4] becomes Missing.

This avoids shifting all subsequent elements, giving constant-number work per removal independent of pool size. But **dense iteration order changes**, and a reference to entity 9's old Component address becomes stale. Vector growth during Emplace can likewise invalidate all saved component pointers.

The implementation declares SparseSet::Remove **noexcept** while moving/assigning Component during swap-removal. If Component's move-assignment throws, the program can terminate. This is a documented source-level exception-safety constraint, not proof that every arbitrary C++ component type is safe.

**Exercise:** compare a trivially movable Position against a custom Component whose move assignment can throw. Study why a separate constrained API or redesigned exception guarantee would be required; do not execute a deliberately terminating test inside an uncontrolled production host.

## 5. Safe insertion is a publication sequence

SparseSet::Emplace first verifies that the full Entity is valid and not already present. A subtle guard also checks that the bare sparse slot is Missing: a different generation occupying that index must not be silently overwritten even if Contains returned false.

It then pushes the Entity onto dense storage and attempts to construct the Component. If construction throws, it pops the new Entity and rethrows. Only after successful Component construction does it write m_Sparse[entity.Index]=dense.

Therefore a failed component constructor must not create a reachable half-component. Registry::Emplace separately checks Entity liveness and reserves a structural journal slot before invoking that storage.

## 6. A two-component join is an algorithm, not a nested scan

Suppose a world has 100,000 Position components but only 50 Velocity components. A naive system could iterate all Positions and probe Velocity, doing 100,000 outer iterations. Registry::Each<Position,Velocity> compares the two dense pool lengths and chooses the **smaller pool** as its driver.

It then checks the other component with a generation-validated sparse lookup, so it makes roughly 50 outer probes instead of 100,000. The callback still receives parameters in the original template order, **Position then Velocity**, even when Velocity is the driver.

The three-component join extends this rule to the smallest of three pools. Its cost is proportional to the smallest pool's size times two checks, plus matched callbacks, rather than a full Cartesian product.

```cpp
// Called once per fixed simulation update after structural commands
// have been applied. The callback changes component *values* only.
registry.Each<Position, Velocity>(
    [dt](kairo::ecs::Entity entity, Position& p, Velocity& v) {
        (void)entity;
        p.x += v.x * dt;
        p.y += v.y * dt;
        p.z += v.z * dt;
        // Do not call Destroy(entity) or Remove<Velocity>(entity)
        // here: swapping dense vectors can invalidate this traversal.
    });
```

**Exercise:** add a third rare PlayerControlled component and compare the number of primary iterations for Each<Position,Velocity,PlayerControlled> with iteration over every Entity followed by three lookups. Use a correctness oracle of matched Entity ID sets, not timing alone.

## 7. The structural journal is bounded and incomplete by design

Registry appends Created, Destroyed, ComponentAdded and ComponentRemoved events with increasing sequence numbers. The journal is limited to a configured capacity, evicting oldest records when full. Its events contain Entity and mutation kind, **not copied component values**.

The journal therefore supports tools, change notifications and diagnostics, but cannot be called a fully replayable save game or lossless rollback log. If a consumer falls behind past the capacity, it must re-extract authoritative state rather than infer lost events.

Registry reserves journal capacity before publishing structural changes so an allocation failure does not leave a successful mutation without space for its event. The type-erased storage map separately owns component pools keyed by C++ RTTI; those keys are not portable schema names.

## 8. Why authoring and runtime storage remain separate

EngineCore::Scene owns hierarchy, stable IDs, serialized components and asset references. SceneECSMirror::Extract creates all ECS entities in one pass, then resolves parent relationships in a second pass because a parent might be encountered later. It clears bootstrap structural events before exposing the mirror.

A subsequent RefreshAuthoredState updates name, local transform, tags, layer, enabled and parent **for an existing mapping**. It does not automatically create/destroy ECS entities or replace runtime-only gameplay components. A larger reconciliation or full re-extraction is needed after topology changes.

## 9. Regression checklist

Use tests to check stale-generation rejection; UINT32_MAX retirement; failed Emplace construction rollback; swap-removal mapping updates; reserve pointer invalidation; two-/three-component callback ordering; structural mutation deferred outside Each; missing component Get; bounded journal eviction; deterministic authored ID translation after scene extraction.

When benchmarking, record N entities, component sparsity, access pattern, cache placement, test SHA, optimization level, compiler and repeated measurements. “O(1) sparse lookup” is a complexity property, not a claim of measured nanoseconds on the target CPU.

*Documentation-only exercise. No C++ compilation or runtime benchmark was executed in this pass.*
