# KairoECS: linked source reading guide

Start with generational entity handles, then sparse-set component storage, then registry query/iteration and its tests. KairoEngineCore scenes and KairoPlayer adapters may use ECS to organize runtime storage, but ECS itself must not own the editor, GPU, asset import or physics simulation.

## Concepts and cross-module APIs

An Entity is a generational handle, not an array index you can hold forever. SparseSet maps stable IDs to dense component slots; Registry owns pools, structural change journals and typed joins. Test stale handle rejection and iteration during mutation.

## Source order (6 files)

1. [`KairoECS.cppm`](KairoECS.cppm) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.
2. [`Entity.cppm`](Entity.cppm) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.
3. [`SparseSet.cppm`](SparseSet.cppm) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.
4. [`Registry.cppm`](Registry.cppm) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.
5. [`benchmarks/ECSBenchmark.cpp`](benchmarks/ECSBenchmark.cpp) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.
6. [`tests/ECSTests.cpp`](tests/ECSTests.cpp) — read the exported API, invariants, dependency imports, implementation and relevant test relationships.

## Reproduce and extend

Use [README.md](README.md) for setup and the relevant build-system/tests for independent verification. A green isolated build is not proof of the complete 27-module Kairo workspace, whose heads must pass [KairoGameEngine acceptance](https://github.com/swayam8624/KairoGameEngine). Original code is [MIT-licensed](LICENSE) for learning, reuse and further development; third-party dependencies retain their licenses. The index is an initial teaching map, not complete per-function annotation.
