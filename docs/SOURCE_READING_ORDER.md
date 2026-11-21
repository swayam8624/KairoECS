# KairoECS — source reading order

This is a small, data-oriented C++23 component library. Read each module and its tests in the order below, then examine how its API is consumed by the larger [KairoGameEngine workspace](https://github.com/swayam8624/KairoGameEngine). The order is conceptual, not a CMake build sequence.

1. [README](../README.md) — understand the role of the ECS as **runtime storage**, not a scene graph, physics solver or serialization system.
2. [CMakeLists.txt](../CMakeLists.txt) — discover compiler/module settings, test targets and benchmark inclusion.
3. [Entity.cppm](../Entity.cppm) — learn `Entity {Index, Generation}`, validity, lifecycle and `StructuralChange`. A generation helps reject stale reused-slot handles; it does not make an entity durable across runs or scene serialization.
4. [SparseSet.cppm](../SparseSet.cppm) — understand the sparse entity-index mapping and dense component storage. Trace `Contains`, `Emplace`, `Get`, `Remove`, and generation checks. Note how swap-removal affects iteration order.
5. [Registry.cppm](../Registry.cppm) — central owner of entity slots and typed component pools. Trace `Create` → `Emplace` → `Each` → `Remove` → `Destroy`. The registry uses sparse-set pools and records bounded structural events.
6. [KairoECS.cppm](../KairoECS.cppm) — umbrella module re-export. Importing `Kairo.ECS` exposes the above module interfaces to dependent code without relying on textual includes.
7. [tests/ECSTests.cpp](../tests/ECSTests.cpp) — validate generations, joins, component lifetime, swap removal, reserve behaviour and mutation journaling.
8. [benchmarks/ECSBenchmark.cpp](../benchmarks/ECSBenchmark.cpp) — measure creation, component access and iteration, distinguishing baseline correctness from performance.
9. [research/README.md](../research/README.md) — future-facing research is not the implementation contract. Check measured results rather than treating status declarations as proof.

## Cross-file reasoning

**Entity lifecycle:** `Registry::Create()` creates an `Entity`; `Registry::Emplace<Component>()` stores the component in a type-specific `SparseSet<Component>`. `Registry::Destroy()` removes components and advances the generation before a slot may be reused. Callers must not keep raw references after structural mutation unless validity is guaranteed.

**Multicomponent query:** `Registry::Each<...>()` chooses a component pool to scan and resolves other component types via sparse lookup. Its query is a runtime view, not an independent owning copy.

**Cross-repository:** Kairo EngineCore supplies higher-level scene/application semantics; KairoAssets owns persistent asset metadata; physics/rendering run as dependent systems. The ECS deliberately does not import any of those libraries.

## Exercises

1. Create an entity with two components, destroy it, reuse the slot and verify that the original handle fails validation.
2. Draw sparse and dense arrays before and after removal of a nonlast component.
3. Explain the trade-off between a dense pool's cache locality and unstable iteration order under swap removal.
4. Find the difference between a structural-change journal entry and an actual component-state snapshot.
5. Run ECS tests and benchmarks, recording compiler and build configuration rather than reporting an unsourced performance figure.

## Licensing

See [LICENSE](../LICENSE) for original KairoECS code and documentation. Code imported later from third parties must retain any additional permissions/notices.
