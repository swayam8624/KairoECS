module;

#include <cstdint>
#include <limits>

export module Kairo.ECS.Entity;

export namespace kairo::ecs
{
    /// Stable process-local handle for an ECS entity. Input: an index and its
    /// generation. Output: a value usable as a component-storage key. Task:
    /// prevent an old handle from silently addressing a recycled entity slot.
    struct Entity final
    {
        static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t Index = InvalidIndex;
        std::uint32_t Generation = 0u;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
        friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;
    };

    /// Describes a structural mutation in monotonic registry order. Component
    /// type identity is intentionally opaque here: gameplay systems should
    /// inspect concrete data, while tooling can count and replay mutations.
    enum class StructuralChangeKind : std::uint8_t { Created, Destroyed, ComponentAdded, ComponentRemoved };

    struct StructuralChange final
    {
        std::uint64_t Sequence = 0u;
        StructuralChangeKind Kind = StructuralChangeKind::Created;
        Entity Subject{};
    };
}
