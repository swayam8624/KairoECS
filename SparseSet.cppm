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
        [[nodiscard]] bool Contains(Entity entity) const noexcept
        {
            if (!entity.IsValid() || entity.Index >= m_Sparse.size()) return false;
            const std::uint32_t dense = m_Sparse[entity.Index];
            return dense != Missing && dense < m_Entities.size() && m_Entities[dense] == entity;
        }

        template<typename... Arguments>
        Component& Emplace(Entity entity, Arguments&&... arguments)
        {
            if (!entity.IsValid()) throw std::invalid_argument("Cannot attach a component to an invalid entity.");
            if (Contains(entity)) throw std::invalid_argument("Entity already owns this component type.");
            EnsureSparseIndex(entity.Index);
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

        [[nodiscard]] Component* TryGet(Entity entity) noexcept
        {
            return Contains(entity) ? &m_Components[m_Sparse[entity.Index]] : nullptr;
        }

        [[nodiscard]] const Component* TryGet(Entity entity) const noexcept
        {
            return Contains(entity) ? &m_Components[m_Sparse[entity.Index]] : nullptr;
        }

        Component& Get(Entity entity)
        {
            Component* component = TryGet(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

        const Component& Get(Entity entity) const
        {
            const Component* component = TryGet(entity);
            if (component == nullptr) throw std::out_of_range("Entity does not own this component type.");
            return *component;
        }

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

        [[nodiscard]] std::size_t Size() const noexcept { return m_Components.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_Components.empty(); }
        [[nodiscard]] const std::vector<Entity>& Entities() const noexcept { return m_Entities; }
        [[nodiscard]] std::vector<Component>& Components() noexcept { return m_Components; }
        [[nodiscard]] const std::vector<Component>& Components() const noexcept { return m_Components; }

    private:
        static constexpr std::uint32_t Missing = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> m_Sparse;
        std::vector<Entity> m_Entities;
        std::vector<Component> m_Components;

        void EnsureSparseIndex(std::uint32_t index)
        {
            if (index >= m_Sparse.size()) m_Sparse.resize(static_cast<std::size_t>(index) + 1u, Missing);
        }

        [[nodiscard]] std::uint32_t CheckedDenseIndex() const
        {
            if (m_Entities.size() >= Missing) throw std::length_error("SparseSet reached its dense-index limit.");
            return static_cast<std::uint32_t>(m_Entities.size());
        }
    };
}
