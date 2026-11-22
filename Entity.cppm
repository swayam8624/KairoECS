// TEACHING MODULE: KairoECS::Entity is a *process-local generation-checked
// slot handle*, not an authored EngineCore::Entity or a world-space object.
// Registry creates/recycles slots; a destroyed slot's generation advances
// so old references cannot accidentally access a newly allocated entity.
//
// Entity::IsValid checks only the sentinel index; Registry::IsAlive also
// checks generation and active-slot bitmap. SparseSet retains the complete
// Entity alongside dense components for that same check. SceneECSMirror in
// KairoGameEngine preserves authored identity in a dedicated component;
// never serialize ECS index/generation as a durable .kscene identity.
//
// StructuralChange is an opaque journal event for tooling, not a
// replayable serialized component snapshot.

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

        /// TEACHING METHOD: Checks only the sentinel index. Registry::IsAlive additionally checks generation and live-slot state; a syntactically valid Entity need not be alive.
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
