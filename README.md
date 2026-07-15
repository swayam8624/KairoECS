# KairoECS

`KairoECS` is the data-oriented runtime-storage foundation for Kairo. It is a
standalone C++23 module library: it has no windowing, renderer, physics, scene
file, asset, or editor dependency.

```text
KairoMath / KairoAssets / KairoEngineCore adapters
                       |
                       v
                    KairoECS
                       |
                       v
              systems, gameplay, simulation
```

V1 deliberately ships a real sparse-set registry rather than a placeholder:

- generational `Entity` handles reject stale recycled slots
- type-erased component-pool ownership with typed `Emplace`, `Get`, `TryGet`,
  `Has`, and `Remove`
- dense component arrays and O(1) sparse lookup/removal
- deterministic one- and two-component joins through `Registry::Each`
- bounded structural-change journal for tools, replay, replication, and future
  archetype migration work
- destruction removes every attached component before a slot can recycle

The registry is intentionally not a scene graph, scheduler, serialization
format, reflection database, or physics world. `KairoEngineCore` remains the
current scene/application surface; adapters can migrate runtime systems to ECS
incrementally without duplicating persistent editor state.

## Conventions and safety

An entity is valid only while its `(Index, Generation)` pair is alive. Destroy
increments the generation before a slot can be reused; a slot at the maximum
generation is retired rather than wrapping. Component storage validates both
index and generation, so stale handles cannot access a newer entity's data.

Component types must be move-assignable because swap-remove keeps dense arrays
compact. Do not add/remove the primary component or destroy entities inside an
`Each` callback: doing so can reorder the dense storage currently being
iterated. Queue structural changes for after the system pass instead.

## Example

```cpp
import Kairo.ECS;

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };

kairo::ecs::Registry registry;
const auto entity = registry.Create();
registry.Emplace<Position>(entity, 0.0f, 1.0f, 0.0f);
registry.Emplace<Velocity>(entity, 2.0f, 0.0f, 0.0f);

registry.Each<Position, Velocity>([](auto, Position& position, Velocity& velocity) {
    position.x += velocity.x;
    position.y += velocity.y;
    position.z += velocity.z;
});
```

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

For the umbrella build, `KairoGameEngine` supplies the portable compiler
presets and owns the pinned integration revision.
