// TEACHING ENTRY POINT: `import Kairo.ECS;` re-exports process-local
// generational handles (Entity), contiguous per-type component storage
// (SparseSet<T>) and the owning Registry.
//
// It creates neither a global ECS singleton nor an authored Scene.
// KairoGameEngine::SceneECSMirror translates persistent EngineCore
// Entity IDs into these ephemeral ECS handles. A serialization format
// must retain AuthoredEntityIdentity rather than slot/generation.
// See docs/SOURCE_READING_ORDER.md and tests/ECSTests.cpp.
export module Kairo.ECS;

export import Kairo.ECS.Entity;
export import Kairo.ECS.SparseSet;
export import Kairo.ECS.Registry;
