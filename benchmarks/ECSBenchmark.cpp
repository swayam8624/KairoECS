#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

import Kairo.ECS;

namespace
{
    struct Position { float x = 0.0f, y = 0.0f, z = 0.0f; };
    struct Velocity { float x = 0.0f, y = 0.0f, z = 0.0f; };
    struct Active { std::uint32_t value = 1u; };
}

int main(int argc, char** argv)
{
    using namespace kairo::ecs;
    std::size_t entityCount = 100000u;
    if (argc > 1)
        entityCount = static_cast<std::size_t>(std::stoull(argv[1]));

    Registry registry;
    registry.ReserveEntities(entityCount);
    registry.Reserve<Position>(entityCount);
    registry.Reserve<Velocity>(entityCount);
    registry.Reserve<Active>(entityCount / 10u + 1u);

    for (std::size_t index = 0; index < entityCount; ++index)
    {
        const Entity entity = registry.Create();
        registry.Emplace<Position>(entity, static_cast<float>(index), 0.0f, 0.0f);
        registry.Emplace<Velocity>(entity, 1.0f, 0.5f, -0.25f);
        if ((index % 10u) == 0u)
            registry.Emplace<Active>(entity, 1u);
    }
    registry.ClearStructuralChanges();

    constexpr std::size_t iterations = 120u;
    const auto start = std::chrono::steady_clock::now();
    std::size_t visits = 0u;
    for (std::size_t frame = 0; frame < iterations; ++frame)
    {
        registry.Each<Position, Velocity, Active>(
            [&visits](Entity, Position& p, Velocity& v, Active&)
            {
                p.x += v.x;
                p.y += v.y;
                p.z += v.z;
                ++visits;
            });
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    const double seconds = static_cast<double>(nanoseconds) / 1.0e9;
    const double visitsPerSecond = seconds > 0.0 ? static_cast<double>(visits) / seconds : 0.0;

    std::cout
        << "{\"schema\":\"kairo.ecs.benchmark.v1\","
        << "\"entities\":" << entityCount << ","
        << "\"iterations\":" << iterations << ","
        << "\"matched_entities\":" << registry.Count<Active>() << ","
        << "\"visits\":" << visits << ","
        << "\"elapsed_ns\":" << nanoseconds << ","
        << "\"visits_per_second\":" << visitsPerSecond
        << "}\n";
}
