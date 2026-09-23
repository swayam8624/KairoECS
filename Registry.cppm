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
        [[nodiscard]] Entity Create()
        {
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
                m_Alive.push_back(true);
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
        bool Destroy(Entity entity)
        {
            if (!IsAlive(entity)) return false;
            for (const auto& storage : m_Storages) (void)storage->Remove(entity);
            m_Alive[entity.Index] = false;
            --m_LiveEntityCount;
            AppendChange(StructuralChangeKind::Destroyed, entity);
            if (m_Generations[entity.Index] == std::numeric_limits<std::uint32_t>::max()) return true;
            ++m_Generations[entity.Index];
            m_FreeIndices.push_back(entity.Index);
            return true;
        }

        [[nodiscard]] bool IsAlive(Entity entity) const noexcept
        {
            return entity.IsValid() && entity.Index < m_Generations.size() &&
                m_Alive[entity.Index] && m_Generations[entity.Index] == entity.Generation;
        }

        [[nodiscard]] std::size_t EntityCount() const noexcept
        {
            return m_LiveEntityCount;
        }

        /// Reserve entity-slot bookkeeping for known runtime population sizes.
        /// This changes capacity only; stable entity indices/generations are
        /// still allocated exclusively by Create().
        void ReserveEntities(std::size_t capacity)
        {
            m_Generations.reserve(capacity);
            m_Alive.reserve(capacity);
            m_FreeIndices.reserve(capacity);
        }

        template<typename Component>
        void Reserve(std::size_t capacity)
        {
            FindOrCreatePool<Component>().Set.Reserve(capacity);
        }

        template<typename Component>
        [[nodiscard]] std::size_t Count() const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return pool == nullptr ? 0u : pool->Set.Size();
        }

        template<typename Component, typename... Arguments>
        Component& Emplace(Entity entity, Arguments&&... arguments)
        {
            RequireAlive(entity);
            Component& component = FindOrCreatePool<Component>().Set.Emplace(entity,
                std::forward<Arguments>(arguments)...);
            AppendChange(StructuralChangeKind::ComponentAdded, entity);
            return component;
        }

        template<typename Component>
        [[nodiscard]] bool Has(Entity entity) const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr && pool->Set.Contains(entity);
        }

        template<typename Component>
        [[nodiscard]] Component* TryGet(Entity entity) noexcept
        {
            Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr ? pool->Set.TryGet(entity) : nullptr;
        }

        template<typename Component>
        [[nodiscard]] const Component* TryGet(Entity entity) const noexcept
        {
            const Pool<Component>* pool = FindPool<Component>();
            return IsAlive(entity) && pool != nullptr ? pool->Set.TryGet(entity) : nullptr;
        }

        template<typename Component>
        Component& Get(Entity entity)
        {
            Component* component = TryGet<Component>(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        template<typename Component>
        const Component& Get(Entity entity) const
        {
            const Component* component = TryGet<Component>(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        template<typename Component>
        bool Remove(Entity entity)
        {
            if (!IsAlive(entity)) return false;
            Pool<Component>* pool = FindPool<Component>();
            if (pool == nullptr || !pool->Set.Remove(entity)) return false;
            AppendChange(StructuralChangeKind::ComponentRemoved, entity);
            return true;
        }

        /// Iterates a dense primary component pool. Precondition: the callback
        /// must not add/remove the primary component or destroy entities during
        /// this call, because those operations can reorder dense storage.
        template<typename Component, typename Callable>
            requires std::invocable<Callable&, Entity, Component&>
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
        void Each(Callable&& callable)
        {
            Pool<First>* first = FindPool<First>();
            Pool<Second>* second = FindPool<Second>();
            if (first == nullptr || second == nullptr) return;

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
        void Each(Callable&& callable)
        {
            Pool<First>* first = FindPool<First>();
            Pool<Second>* second = FindPool<Second>();
            Pool<Third>* third = FindPool<Third>();
            if (first == nullptr || second == nullptr || third == nullptr) return;

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

        [[nodiscard]] std::vector<StructuralChange> StructuralChanges() const { return m_Changes; }
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

        void RequireAlive(Entity entity) const
        {
            if (!IsAlive(entity)) throw std::invalid_argument("ECS operation requires a live entity.");
        }

        void AppendChange(StructuralChangeKind kind, Entity entity)
        {
            if (m_Changes.size() == m_JournalCapacity) m_Changes.erase(m_Changes.begin());
            m_Changes.push_back({ ++m_NextChangeSequence, kind, entity });
        }

        template<typename Component>
        [[nodiscard]] Pool<Component>* FindPool() noexcept
        {
            const auto found = m_Pools.find(std::type_index(typeid(Component)));
            return found == m_Pools.end() ? nullptr : static_cast<Pool<Component>*>(found->second.get());
        }

        template<typename Component>
        [[nodiscard]] const Pool<Component>* FindPool() const noexcept
        {
            const auto found = m_Pools.find(std::type_index(typeid(Component)));
            return found == m_Pools.end() ? nullptr : static_cast<const Pool<Component>*>(found->second.get());
        }

        template<typename Component>
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
