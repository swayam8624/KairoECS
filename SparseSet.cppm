// TEACHING FILE GUIDE: Dense per-component storage with sparse entity-index lookup. Entity.Index locates a sparse slot; matching Entity.Generation guards against recycled handles. Registry allocates a SparseSet per component type; tests exercise presence, reallocation and swap-removal semantics. Removing one component can move the last dense component and invalidate references/iterators.
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

export module Kairo.ECS.SparseSet;

import Kairo.ECS.Entity;

export namespace kairo::ecs
{
    /// Dense component storage indexed through a sparse entity-index table.
    /// Input: valid Entity handles and component values. Output: O(1) average
    /// lookup, insertion, and removal. Task: provide cache-friendly component
    /// iteration while validating generation before returning a component.
    template<typename Component>
    class SparseSet final
    {
    public:
        /// TEACHING METHOD: O(1) membership test, verifying both dense index and full generational Entity equality. An old handle cannot see a new entity's component.
        [[nodiscard]] bool Contains(Entity entity) const noexcept
        {
            if (!entity.IsValid() || entity.Index >= m_Sparse.size()) return false;
            const std::uint32_t dense = m_Sparse[entity.Index];
            return dense != Missing && dense < m_Entities.size() && m_Entities[dense] == entity;
        }

        template<typename... Arguments>
        /// TEACHING METHOD: Add a component for a currently unoccupied slot; construct in dense storage then publish sparse map entry. Registry checks entity life before calling.
        Component& Emplace(Entity entity, Arguments&&... arguments)
        {
            if (!entity.IsValid()) throw std::invalid_argument("Cannot attach a component to an invalid entity.");
            if (Contains(entity)) throw std::invalid_argument("Entity already owns this component type.");
            EnsureSparseIndex(entity.Index);
            // Contains() compares generations. An occupied slot from a
            // different generation would otherwise bypass that check and
            // overwrite the sparse lookup, leaving a permanently unreachable
            // dense component behind. Require explicit removal before reuse.
            if (m_Sparse[entity.Index] != Missing)
                throw std::invalid_argument("SparseSet entity index is still occupied by another generation.");
            const std::uint32_t dense = CheckedDenseIndex();
            m_Entities.push_back(entity);
            try
            {
                m_Components.emplace_back(std::forward<Arguments>(arguments)...);
            }
            catch (...)
            {
                m_Entities.pop_back();
                throw;
            }
            m_Sparse[entity.Index] = dense;
            return m_Components.back();
        }

        /// TEACHING METHOD: Return nullable pointer to component without throwing when absent; pointer validity ends on vector reallocation or swap-removal.
        [[nodiscard]] Component* TryGet(Entity entity) noexcept
        {
            return Contains(entity) ? &m_Components[m_Sparse[entity.Index]] : nullptr;
        }

        /// TEACHING METHOD: Read-only nullable lookup; returns null for a stale generation or missing component and never permits mutation.
        [[nodiscard]] const Component* TryGet(Entity entity) const noexcept
        {
            return Contains(entity) ? &m_Components[m_Sparse[entity.Index]] : nullptr;
        }

        /// TEACHING METHOD: Strict mutable lookup: throw on absence; client systems should check Has/TryGet for optional behavior.
        Component& Get(Entity entity)
        {
            Component* component = TryGet(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        /// TEACHING METHOD: Strict const lookup; throws on absence while preserving immutability of the returned component.
        const Component& Get(Entity entity) const
        {
            const Component* component = TryGet(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        /// TEACHING METHOD: Swap the last dense entry into the removed slot to keep contiguous storage; update moved entity's sparse index. Order changes and references can be invalidated. Current noexcept requires care if Component move assignment may throw.
        bool Remove(Entity entity) noexcept
        {
            if (!Contains(entity)) return false;
            const std::uint32_t dense = m_Sparse[entity.Index];
            const std::uint32_t last = static_cast<std::uint32_t>(m_Entities.size() - 1u);
            if (dense != last)
            {
                m_Entities[dense] = std::move(m_Entities[last]);
                m_Components[dense] = std::move(m_Components[last]);
                m_Sparse[m_Entities[dense].Index] = dense;
            }
            m_Entities.pop_back();
            m_Components.pop_back();
            m_Sparse[entity.Index] = Missing;
            return true;
        }

        /// Reserves dense component/entity storage. Sparse lookup growth remains
        /// entity-index driven, so reserving components never changes handle
        /// validity or allocates a speculative sparse table.
        /// TEACHING METHOD: Reserve dense entity/component array capacity to reduce allocations; reservation itself can move previously returned component references.
        void Reserve(std::size_t capacity)
        {
            m_Entities.reserve(capacity);
            m_Components.reserve(capacity);
        }

        /// TEACHING METHOD: Number of components stored, not count of Registry entities.
        [[nodiscard]] std::size_t Size() const noexcept { return m_Components.size(); }
        /// TEACHING METHOD: Report whether this component type currently has any entries.
        [[nodiscard]] bool Empty() const noexcept { return m_Components.empty(); }
        /// TEACHING METHOD: Expose dense entity order for efficient Registry::Each joins. Structural changes may reorder the vector.
        [[nodiscard]] const std::vector<Entity>& Entities() const noexcept { return m_Entities; }
        /// TEACHING METHOD: Expose contiguous component data for cache-friendly system iteration; do not mutate structure while iterating.
        [[nodiscard]] std::vector<Component>& Components() noexcept { return m_Components; }
        /// TEACHING METHOD: Const dense component view for system iteration; structural changes can invalidate references even without direct mutation here.
        [[nodiscard]] const std::vector<Component>& Components() const noexcept { return m_Components; }

    private:
        static constexpr std::uint32_t Missing = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> m_Sparse;
        std::vector<Entity> m_Entities;
        std::vector<Component> m_Components;

        /// TEACHING METHOD: Extend sparse index table on demand with Missing sentinels, leaving dense storage unchanged.
        void EnsureSparseIndex(std::uint32_t index)
        {
            if (index >= m_Sparse.size()) m_Sparse.resize(static_cast<std::size_t>(index) + 1u, Missing);
        }

        /// TEACHING METHOD: Verify dense storage can be represented by the 32-bit sparse slot encoding before insertion.
        [[nodiscard]] std::uint32_t CheckedDenseIndex() const
        {
            if (m_Entities.size() >= Missing) throw std::length_error("SparseSet reached its dense-index limit.");
            return static_cast<std::uint32_t>(m_Entities.size());
        }
    };
}
