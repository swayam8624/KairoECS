# Teaching source map — KairoECS

Data-oriented entity storage, generational IDs and sparse-set component joins.

Follow source links sequentially. For each API/function, identify its input contract, owning data structure, called modules, failure modes, outputs, test and downstream consumers.

## Read in this order

1. [`CMakeLists.txt`](../CMakeLists.txt)
2. [`KairoECS.cppm`](../KairoECS.cppm)
3. [`Entity.cppm`](../Entity.cppm)
4. [`SparseSet.cppm`](../SparseSet.cppm)
5. [`Registry.cppm`](../Registry.cppm)
6. [`tests`](../tests)
7. [`benchmarks`](../benchmarks)
8. [`research`](../research)

## How this repository interacts with others

EngineCore owns authored scene representation; ECS is runtime storage and does not implicitly serialize scenes or own physics/renderer resources. Test stale handles and structural mutation during iteration.

## Verification and study rules

- Read the public C++23 module/entrypoint first, then its implementation and tests.
- Explain invariants, ownership, error states, concurrency or numerical assumptions in comments, not trivial syntax.
- A passing CI job covers only tests actually run on that commit/host; source presence is not runtime certification.
- Check [STATUS.md](../STATUS.md) and [STATUS.yaml](../STATUS.yaml) for current explicit verification limitations.
- Original work may be modified and extended under the repository license; third-party content retains upstream terms.


## Complete source-file inventory

After the curated teaching path, follow this deterministic inventory of first-party source, scripts, shaders, and tests. Grouping is a suggested learning order, not an indication that every function is already annotated. Vendored dependencies, ROMs, binaries, data, media, and generated evidence are excluded.

### Entry points and build (1)

1. [`CMakeLists.txt`](../CMakeLists.txt)

### Interfaces and modules (4)

1. [`Entity.cppm`](../Entity.cppm)
2. [`KairoECS.cppm`](../KairoECS.cppm)
3. [`Registry.cppm`](../Registry.cppm)
4. [`SparseSet.cppm`](../SparseSet.cppm)

### Research and benchmarks (1)

1. [`benchmarks/ECSBenchmark.cpp`](../benchmarks/ECSBenchmark.cpp)

### Verification and tests (1)

1. [`tests/ECSTests.cpp`](../tests/ECSTests.cpp)

Snapshot totals: 7 first-party source/build/test files, 17 tracked files overall. Re-audit when files are renamed or added.
