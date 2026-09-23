#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

import Kairo.ECS;

using namespace kairo::ecs;

namespace
{
    struct Position final { int X = 0; int Y = 0; };
    struct Velocity final { int X = 0; int Y = 0; };
    struct Health final { int Value = 100; };
}

TEST_CASE("Registry invalidates stale entities before recycling slots", "[KairoECS][Entity]")
{
    Registry registry;
    const Entity first = registry.Create();
    REQUIRE(registry.IsAlive(first));
    REQUIRE(registry.Destroy(first));
    CHECK_FALSE(registry.IsAlive(first));
    CHECK_FALSE(registry.Destroy(first));

    const Entity recycled = registry.Create();
    CHECK(recycled.Index == first.Index);
    CHECK(recycled.Generation != first.Generation);
    CHECK(registry.IsAlive(recycled));
    CHECK(registry.EntityCount() == 1u);
}

TEST_CASE("Sparse component storage validates ownership and removal", "[KairoECS][Components]")
{
    Registry registry;
    const Entity entity = registry.Create();
    Position& position = registry.Emplace<Position>(entity, 2, 5);
    CHECK(position.X == 2);
    CHECK(registry.Has<Position>(entity));
    CHECK(registry.TryGet<Velocity>(entity) == nullptr);
    REQUIRE_THROWS_AS(registry.Emplace<Position>(entity, 7, 9), std::invalid_argument);

    registry.Get<Position>(entity).Y = 11;
    CHECK(registry.Get<Position>(entity).Y == 11);
    CHECK(registry.Remove<Position>(entity));
    CHECK_FALSE(registry.Remove<Position>(entity));
    REQUIRE_THROWS_AS(registry.Get<Position>(entity), std::out_of_range);
}

TEST_CASE("Registry joins dense components and journals structural changes", "[KairoECS][Iteration][Journal]")
{
    Registry registry(5u);
    const Entity moving = registry.Create();
    const Entity staticEntity = registry.Create();
    registry.Emplace<Position>(moving, 3, -2);
    registry.Emplace<Velocity>(moving, 4, 7);
    registry.Emplace<Position>(staticEntity, 9, 9);
    registry.Emplace<Health>(staticEntity, 25);

    registry.Each<Position, Velocity>([](Entity, Position& position, Velocity& velocity)
    {
        position.X += velocity.X;
        position.Y += velocity.Y;
    });
    CHECK(registry.Get<Position>(moving).X == 7);
    CHECK(registry.Get<Position>(moving).Y == 5);
    CHECK(registry.Get<Position>(staticEntity).X == 9);

    std::vector<Entity> healthOwners;
    registry.Each<Health>([&healthOwners](Entity entity, Health& health)
    {
        health.Value -= 1;
        healthOwners.push_back(entity);
    });
    REQUIRE(healthOwners == std::vector<Entity>{ staticEntity });
    CHECK(registry.Get<Health>(staticEntity).Value == 24);

    const auto changes = registry.StructuralChanges();
    REQUIRE(changes.size() == 5u);
    CHECK(changes.front().Sequence == 2u);
    CHECK(changes.back().Kind == StructuralChangeKind::ComponentAdded);
    registry.ClearStructuralChanges();
    CHECK(registry.StructuralChanges().empty());
}

TEST_CASE("Destroy removes every attached component and rejects stale mutation", "[KairoECS][Lifecycle]")
{
    Registry registry;
    const Entity entity = registry.Create();
    registry.Emplace<Position>(entity, 1, 2);
    registry.Emplace<Velocity>(entity, 3, 4);
    REQUIRE(registry.Destroy(entity));
    CHECK_FALSE(registry.Has<Position>(entity));
    CHECK_FALSE(registry.Has<Velocity>(entity));
    REQUIRE_THROWS_AS(registry.Emplace<Health>(entity, 1), std::invalid_argument);
}


TEST_CASE("Registry reserves capacity and joins three components through the rarest pool", "[KairoECS][Iteration][Runtime]")
{
    Registry registry;
    registry.ReserveEntities(256u);
    registry.Reserve<Position>(256u);
    registry.Reserve<Velocity>(256u);
    registry.Reserve<Health>(8u);

    std::vector<Entity> entities;
    for (int index = 0; index < 128; ++index)
    {
        const Entity entity = registry.Create();
        entities.push_back(entity);
        registry.Emplace<Position>(entity, index, index * 2);
        if ((index % 2) == 0)
            registry.Emplace<Velocity>(entity, 1, -1);
        if ((index % 16) == 0)
            registry.Emplace<Health>(entity, 100);
    }

    CHECK(registry.Count<Position>() == 128u);
    CHECK(registry.Count<Velocity>() == 64u);
    CHECK(registry.Count<Health>() == 8u);

    std::size_t joined = 0u;
    registry.Each<Position, Velocity, Health>(
        [&joined](Entity, Position& position, Velocity& velocity, Health& health)
        {
            position.X += velocity.X;
            health.Value -= 1;
            ++joined;
        });

    CHECK(joined == 8u);
    CHECK(registry.Get<Position>(entities.front()).X == 1);
    CHECK(registry.Get<Health>(entities.front()).Value == 99);
}

TEST_CASE("Two component joins are symmetric in population size but stable in callback order", "[KairoECS][Iteration]")
{
    Registry registry;
    const Entity commonA = registry.Create();
    const Entity commonB = registry.Create();
    registry.Emplace<Position>(commonA, 1, 2);
    registry.Emplace<Position>(commonB, 3, 4);
    registry.Emplace<Velocity>(commonB, 5, 6);

    std::size_t visited = 0u;
    registry.Each<Position, Velocity>(
        [&visited](Entity entity, Position& position, Velocity& velocity)
        {
            CHECK(entity.IsValid());
            position.X += velocity.X;
            ++visited;
        });

    CHECK(visited == 1u);
    CHECK(registry.Get<Position>(commonB).X == 8);
}
