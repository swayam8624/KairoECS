// TEACHING MAP: Registry owns generation-checked entity slots,
 // per-component SparseSet pools and a bounded structural-change journal.
 // It is process-local runtime storage, not a serialized authored Scene.
 // Create/Destroy enforce generation retirement; Each joins scan the
 // smallest matching dense pool and query other components by Entity.
 // Mutation of a primary component during iteration can swap compact
 // the pool and invalidate references; queue structural edits instead.
 // KairoGameEngine::SceneECSMirror maps durable EngineCore Entity IDs
 // onto these transient generation-based handles.
module;

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

export module Kairo.ECS.Registry;

import Kairo.ECS.Entity;
import Kairo.ECS.SparseSet;

export namespace kairo::ecs
{
    /// Allocation-free during normal component lookup registry. Entity slots
    /// use generation validation; each component type uses an independent
    /// SparseSet. Structural changes are journaled for tooling, replication,
    /// and future archetype migration without imposing a scene graph here.
    class Registry final
    {
    public:
        /// TEACHING METHOD: Initialize registry with bounded structural change journal. Entity generation tables and component pools remain empty until first use.
        explicit Registry(std::size_t structuralJournalCapacity = 4096u)
            : m_JournalCapacity(structuralJournalCapacity)
        {
            if (structuralJournalCapacity == 0u)
                throw std::invalid_argument("Registry structural journal capacity must be positive.");
        }

        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;
        Registry(Registry&&) noexcept = default;
        Registry& operator=(Registry&&) noexcept = default;

        /// Output: a newly alive generational Entity. Task: recycle only slots
        /// whose generation cannot wrap, so stale handles never become valid.
        /// TEACHING METHOD: Allocate or recycle an entity slot, incrementing lifetime sequence and emitting Created. Stale handles are rejected by generation.
        // Runtime index/generation identity is allocated here. It is
        // distinct from the durable Scene Entity ID stored in a project.
        // Allocate an ECS slot either from the free list or by extending the arrays.
        // Reserve a structural journal slot first, so an allocation failure cannot
        // expose a live but unreported entity. EngineCore authored IDs remain separate.
        [[nodiscard]] Entity Create()
        {
            // Reserve the journal entry before changing the alive set. An
            // allocation failure must not leak an entity the caller never got.
            ReserveJournalSlot();
            std::uint32_t index = Entity::InvalidIndex;
            if (!m_FreeIndices.empty())
            {
                index = m_FreeIndices.back();
                m_FreeIndices.pop_back();
                m_Alive[index] = true;
            }
            else
            {
                if (m_Generations.size() >= Entity::InvalidIndex)
                    throw std::length_error("Registry reached its entity-index limit.");
                index = static_cast<std::uint32_t>(m_Generations.size());
                m_Generations.push_back(1u);
                try
                {
                    m_Alive.push_back(true);
                }
                catch (...)
                {
                    m_Generations.pop_back();
                    throw;
                }
            }
            ++m_LiveEntityCount;
            const Entity entity{ index, m_Generations[index] };
            AppendChange(StructuralChangeKind::Created, entity);
            return entity;
        }

        /// Input: a live entity. Output: true only when the entity was alive.
        /// Task: remove all attached components before invalidating the slot.
        /// The final generation is retired instead of wrapping to preserve the
        /// stale-handle invariant for the process lifetime.
        /// TEACHING METHOD: Reject stale handles, remove all owned components, record Destroyed and recycle a slot only when generation can safely advance.
        // Remove every component, retire or advance the generation,
        // and append a Destroyed journal event. Stale generation
        // handles cannot address a subsequently recycled slot.
        // Validate full generation, strip components from every type-erased pool and
        // append Destroyed. Increment generation before slot reuse; slots at UINT32_MAX
        // are permanently retired to prevent stale-handle revival.
        bool Destroy(Entity entity)
        {
            if (!IsAlive(entity)) return false;
            ReserveJournalSlot();
            const bool recyclable =
                m_Generations[entity.Index] != std::numeric_limits<std::uint32_t>::max();
            if (recyclable)
                m_FreeIndices.reserve(m_FreeIndices.size() + 1u);
            // From this point onward, journal append and index recycling are
            // allocation-free. Do not invalidate a live handle first and then
            // risk throwing before its generation is advanced/recycled.
            for (const auto& storage : m_Storages) (void)storage->Remove(entity);
            m_Alive[entity.Index] = false;
            --m_LiveEntityCount;
            AppendChange(StructuralChangeKind::Destroyed, entity);
            if (!recyclable) return true;
            ++m_Generations[entity.Index];
            m_FreeIndices.push_back(entity.Index);
            return true;
        }

        /// TEACHING METHOD: Require valid index, live bitmap and matching generation; Entity::IsValid alone is insufficient.
        // A non-sentinel Entity index is not proof of life. Check index bounds, alive
        // bitmap and generation equality before returning or mutating components.
        [[nodiscard]] bool IsAlive(Entity entity) const noexcept
        {
            return entity.IsValid() && entity.Index < m_Generations.size() &&
                m_Alive[entity.Index] && m_Generations[entity.Index] == entity.Generation;
        }

        /// TEACHING METHOD: Return live entity count, distinct from the number of components in any pool.
        [[nodiscard]] std::size_t EntityCount() const noexcept
        {
            return m_LiveEntityCount;
        }

        /// Reserve entity-slot bookkeeping for known runtime population sizes.
        /// This changes capacity only; stable entity indices/generations are
        /// still allocated exclusively by Create().
        /// TEACHING METHOD: Preallocate slot/generation/free-list arrays; useful before gameplay spawning or simulation imports.
        void ReserveEntities(std::size_t capacity)
        {
            m_Generations.reserve(capacity);
            m_Alive.reserve(capacity);
            m_FreeIndices.reserve(capacity);
        }

        template<typename Component>
        /// TEACHING METHOD: Preallocate one component type's SparseSet to avoid reallocations during bulk insertion.
        void Reserve(std::size_t capacity)
        {
            FindOrCreatePool<Component>().Set.Reserve(capacity);
        }

        template<typename Component>
        /// TEACHING METHOD: Count objects holding a specific component type, not all entities.
        [[nodiscard]] std::size_t Count() const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return pool == nullptr ? 0u : pool->Set.Size();
        }

        template<typename Component, typename... Arguments>
        /// TEACHING METHOD: Require live Entity, insert into type-specific SparseSet and record ComponentAdded after successful construction.
        // Require a live generation, reserve journal space, then construct the
        // Component in its SparseSet. Publish ComponentAdded only after construction
        // succeeds; the journal does not serialize component data.
        Component& Emplace(Entity entity, Arguments&&... arguments)
        {
            RequireAlive(entity);
            ReserveJournalSlot();
            Component& component = FindOrCreatePool<Component>().Set.Emplace(entity,
                std::forward<Arguments>(arguments)...);
            AppendChange(StructuralChangeKind::ComponentAdded, entity);
            return component;
        }

        template<typename Component>
        /// TEACHING METHOD: Test whether a live Entity has a particular component without throwing.
        [[nodiscard]] bool Has(Entity entity) const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr && pool->Set.Contains(entity);
        }

        template<typename Component>
        /// TEACHING METHOD: Return nullable mutable component pointer after generational liveness check.
        [[nodiscard]] Component* TryGet(Entity entity) noexcept
        {
            Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr ? pool->Set.TryGet(entity) : nullptr;
        }

        template<typename Component>
        /// TEACHING METHOD: Read-only nullable component pointer; returned address is subject to dense-vector invalidation.
        [[nodiscard]] const Component* TryGet(Entity entity) const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr ? pool->Set.TryGet(entity) : nullptr;
        }

        template<typename Component>
        /// TEACHING METHOD: Strict mutable lookup: throw if handle is stale or component absent.
        Component& Get(Entity entity)
        {
            Component* component = TryGet<Component>(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        template<typename Component>
        /// TEACHING METHOD: Strict immutable lookup; checked access for systems requiring component presence.
        const Component& Get(Entity entity) const
        {
            const Component* component = TryGet<Component>(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        template<typename Component>
        /// TEACHING METHOD: Remove only a component from a live Entity, journaling structural change; entity itself remains alive.
        // Remove just this Component, not its Entity. Dense swap-removal may relocate
        // an unrelated component and invalidate pointers held by iteration; defer such
        // mutations until after system callbacks.
        bool Remove(Entity entity)
        {
            if (!IsAlive(entity)) return false;
            Pool<Component>* pool = FindPool<Component>();
            if (pool == nullptr || !pool->Set.Contains(entity)) return false;
            ReserveJournalSlot();
            if (!pool->Set.Remove(entity)) return false;
            AppendChange(StructuralChangeKind::ComponentRemoved, entity);
            return true;
        }

        // Any mutation that swaps/removes the iterated component can
        // invalidate its dense references and traversal length.
        /// Iterates a dense primary component pool. Precondition: the callback
        /// must not add/remove the primary component or destroy entities during
        /// this call, because those operations can reorder dense storage.
        template<typename Component, typename Callable>
            requires std::invocable<Callable&, Entity, Component&>
        /// TEACHING METHOD: Join component pools for system updates. The single/two/three-component versions iterate dense data and use sparse lookup for other requirements; never add/remove matching components inside callback.
        // Single component Each: capture the dense pool length and iterate
        // Entity+Component in lockstep. Do not remove/insert that type or destroy
        // entities in callback: swap-removal and vector growth invalidate references.
        void Each(Callable&& callable)
        {
            Pool<Component>* pool = FindPool<Component>();
            if (pool == nullptr) return;
            const std::size_t count = pool->Set.Size();
            const auto& entities = pool->Set.Entities();
            auto& components = pool->Set.Components();
            for (std::size_t index = 0u; index < count; ++index)
            {
                const Entity entity = entities[index];
                if (IsAlive(entity)) std::invoke(callable, entity, components[index]);
            }
        }

        /// Two-component join scans the smaller dense pool and resolves the
        /// other component through O(1) sparse lookup. This preserves callback
        /// argument order while avoiding pathological scans when one component
        /// is much rarer than the other.
        template<typename First, typename Second, typename Callable>
            requires std::invocable<Callable&, Entity, First&, Second&>
        /// TEACHING METHOD: Join component pools for system updates. The single/two/three-component versions iterate dense data and use sparse lookup for other requirements; never add/remove matching components inside callback.
        // Two component Each: pool selection below keeps the smaller side as outer
        // iteration; Has/TryGet safeguards the other side. Avoid structural mutations
        // of either pool during callback.
        void Each(Callable&& callable)
        {
            Pool<First>* first = FindPool<First>();
            Pool<Second>* second = FindPool<Second>();
            if (first == nullptr || second == nullptr) return;

            // For two-component joins, scan the smaller dense pool, then probe the other
            // via generation-checked sparse lookup. Callback arguments stay First,Second
            // regardless of chosen driving pool.
            if (first->Set.Size() <= second->Set.Size())
            {
                const auto& entities = first->Set.Entities();
                auto& components = first->Set.Components();
                for (std::size_t index = 0u; index < first->Set.Size(); ++index)
                {
                    const Entity entity = entities[index];
                    Second* other = IsAlive(entity) ? second->Set.TryGet(entity) : nullptr;
                    if (other != nullptr) std::invoke(callable, entity, components[index], *other);
                }
                return;
            }

            const auto& entities = second->Set.Entities();
            auto& components = second->Set.Components();
            for (std::size_t index = 0u; index < second->Set.Size(); ++index)
            {
                const Entity entity = entities[index];
                First* other = IsAlive(entity) ? first->Set.TryGet(entity) : nullptr;
                if (other != nullptr) std::invoke(callable, entity, *other, components[index]);
            }
        }

        /// Three-component join follows the same smallest-pool rule. Runtime
        /// systems commonly require transform + velocity + domain state; making
        /// this a native query avoids nesting lookups or scanning all entities.
        template<typename First, typename Second, typename Third, typename Callable>
            requires std::invocable<Callable&, Entity, First&, Second&, Third&>
        /// TEACHING METHOD: Join component pools for system updates. The single/two/three-component versions iterate dense data and use sparse lookup for other requirements; never add/remove matching components inside callback.
        // Three component Each: the smallest of three pools becomes the driver while
        // the other two use sparse checked lookup. Dynamic component mutation in
        // callback can corrupt traversal assumptions.
        void Each(Callable&& callable)
        {
            Pool<First>* first = FindPool<First>();
            Pool<Second>* second = FindPool<Second>();
            Pool<Third>* third = FindPool<Third>();
            if (first == nullptr || second == nullptr || third == nullptr) return;

            // For three-component joins, choose the smallest of three dense pools and probe
            // the other two; this reduces scans when one component type is rare, while
            // retaining template argument order.
            if (first->Set.Size() <= second->Set.Size() && first->Set.Size() <= third->Set.Size())
            {
                const auto& entities = first->Set.Entities();
                auto& components = first->Set.Components();
                for (std::size_t index = 0u; index < first->Set.Size(); ++index)
                {
                    const Entity entity = entities[index];
                    Second* b = IsAlive(entity) ? second->Set.TryGet(entity) : nullptr;
                    Third* c = IsAlive(entity) ? third->Set.TryGet(entity) : nullptr;
                    if (b != nullptr && c != nullptr)
                        std::invoke(callable, entity, components[index], *b, *c);
                }
                return;
            }

            if (second->Set.Size() <= third->Set.Size())
            {
                const auto& entities = second->Set.Entities();
                auto& components = second->Set.Components();
                for (std::size_t index = 0u; index < second->Set.Size(); ++index)
                {
                    const Entity entity = entities[index];
                    First* a = IsAlive(entity) ? first->Set.TryGet(entity) : nullptr;
                    Third* c = IsAlive(entity) ? third->Set.TryGet(entity) : nullptr;
                    if (a != nullptr && c != nullptr)
                        std::invoke(callable, entity, *a, components[index], *c);
                }
                return;
            }

            const auto& entities = third->Set.Entities();
            auto& components = third->Set.Components();
            for (std::size_t index = 0u; index < third->Set.Size(); ++index)
            {
                const Entity entity = entities[index];
                First* a = IsAlive(entity) ? first->Set.TryGet(entity) : nullptr;
                Second* b = IsAlive(entity) ? second->Set.TryGet(entity) : nullptr;
                if (a != nullptr && b != nullptr)
                    std::invoke(callable, entity, *a, *b, components[index]);
            }
        }

        /// TEACHING METHOD: Copy the bounded mutation journal for tooling or deterministic diagnostics. It is not the complete authoritative entity history.
        [[nodiscard]] std::vector<StructuralChange> StructuralChanges() const { return m_Changes; }
        /// TEACHING METHOD: Discard retained journal entries after a consumer checkpoint; does not roll back ECS state or sequence numbering.
        void ClearStructuralChanges() noexcept { m_Changes.clear(); }

    private:
        struct StorageBase
        {
            virtual ~StorageBase() = default;
            virtual bool Remove(Entity entity) noexcept = 0;
        };

        template<typename Component>
        struct Pool final : StorageBase
        {
            SparseSet<Component> Set;
            bool Remove(Entity entity) noexcept override { return Set.Remove(entity); }
        };

        std::vector<std::uint32_t> m_Generations;
        std::vector<bool> m_Alive;
        std::vector<std::uint32_t> m_FreeIndices;
        std::size_t m_LiveEntityCount = 0u;
        std::unordered_map<std::type_index, std::unique_ptr<StorageBase>> m_Pools;
        std::vector<StorageBase*> m_Storages;
        std::size_t m_JournalCapacity;
        std::uint64_t m_NextChangeSequence = 0u;
        std::vector<StructuralChange> m_Changes;

        /// TEACHING METHOD: Guard internal mutating APIs against stale handles before changing storage.
        void RequireAlive(Entity entity) const
        {
            if (!IsAlive(entity)) throw std::invalid_argument("ECS operation requires a live entity.");
        }

        /// TEACHING METHOD: Reserve capacity before mutations so allocation failure occurs before partially applying a state change.
        // Prepare journal allocation before a structural mutation. Retained history is
        // bounded; older events may be evicted and cannot provide lossless replay when
        // consumers lag behind.
        void ReserveJournalSlot()
        {
            // Reserve before each structural mutation, not afterward. When the
            // bounded journal is full, AppendChange reuses existing capacity.
            if (m_Changes.size() < m_JournalCapacity &&
                m_Changes.size() == m_Changes.capacity())
            {
                // Grow geometrically to avoid turning the first several
                // thousand entity operations into quadratic reallocations.
                const std::size_t oldCapacity = m_Changes.capacity();
                const std::size_t doubled = oldCapacity > m_JournalCapacity / 2u
                    ? m_JournalCapacity : oldCapacity * 2u;
                const std::size_t nextCapacity = doubled == 0u ? 1u : doubled;
                m_Changes.reserve(nextCapacity);
            }
        }

        /// TEACHING METHOD: Add monotonic sequence ID and mutation type; retain only bounded recent history, dropping oldest records when full.
        // Append monotonic sequence, kind, and Entity identity, not a serialized
        // before/after component snapshot. The bounded journal is a
        // diagnostic/change-notification channel.
        void AppendChange(StructuralChangeKind kind, Entity entity)
        {
            if (m_Changes.size() == m_JournalCapacity) m_Changes.erase(m_Changes.begin());
            m_Changes.push_back({ ++m_NextChangeSequence, kind, entity });
        }

        template<typename Component>
        /// TEACHING METHOD: Locate the type-erased component pool for one C++ type without allocating a new pool.
        [[nodiscard]] Pool<Component>* FindPool() noexcept
        {
            const auto found = m_Pools.find(std::type_index(typeid(Component)));
            return found == m_Pools.end() ? nullptr : static_cast<Pool<Component>*>(found->second.get());
        }

        template<typename Component>
        /// TEACHING METHOD: Const lookup of an existing typed pool; callers cannot mutate the Registry through the result.
        [[nodiscard]] const Pool<Component>* FindPool() const noexcept
        {
            const auto found = m_Pools.find(std::type_index(typeid(Component)));
            return found == m_Pools.end() ? nullptr : static_cast<const Pool<Component>*>(found->second.get());
        }

        template<typename Component>
        /// TEACHING METHOD: Create a new per-type SparseSet lazily, maintaining type map and iterable storage list with exception rollback.
        // Lazy component-pool construction uses process-local RTTI. If insertion into
        // owning m_Pools fails, undo the pointer added to m_Storages. RTTI identifiers
        // must not become persisted schema keys.
        Pool<Component>& FindOrCreatePool()
        {
            if (Pool<Component>* existing = FindPool<Component>(); existing != nullptr) return *existing;
            auto pool = std::make_unique<Pool<Component>>();
            Pool<Component>* result = pool.get();
            m_Storages.push_back(result);
            try
            {
                m_Pools.emplace(std::type_index(typeid(Component)), std::move(pool));
            }
            catch (...)
            {
                m_Storages.pop_back();
                throw;
            }
            return *result;
        }
    };
}
