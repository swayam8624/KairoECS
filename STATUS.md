# KairoECS Status

Wave: B — runtime-infrastructure completion  
Frozen v1 target: 80/100  
Source gate: complete  
Execution gate: unit tests + deterministic benchmark + KairoGameEngine Scene/ECS bridge test

## Frozen v1 scope

KairoECS v1 is a generational sparse-set runtime store with dense component iteration, bounded structural journaling, capacity reservation, one/two/three-component queries and deterministic behavior. Persistent scene serialization remains in EngineCore. Adaptive archetype migration is research work, not required for v1.

## 80 exit evidence

- Stale generation handles are rejected and destroyed entities remove all components.
- Runtime systems can reserve entity/component capacity before bulk extraction.
- Two- and three-component joins scan the smallest participating dense pool while preserving callback type order.
- A machine-readable 100k-entity benchmark target exercises a representative rare-component query.
- KairoGameEngine owns an explicit one-way Scene → ECS extraction bridge retaining stable authored IDs beside process-local ECS handles.
- Authoring changes enter runtime storage only through an explicit refresh boundary.

## Post-80 direction

The MORPH-ECS research track compares sparse, archetype and adaptive layouts. It must not destabilize the v1 sparse-set API before experiments establish a benefit.
