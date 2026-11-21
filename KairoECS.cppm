// SOURCE STUDY NOTE:
// Umbrella C++23 module for the standalone ECS library: re-exports entity handles, sparse-set
// components and registry management so consumers can import Kairo.ECS. The implementations live in
// Entity.cppm, SparseSet.cppm and Registry.cppm.
// Follow docs/SOURCE_READING_ORDER.md for the cross-file call path.

export module Kairo.ECS;

export import Kairo.ECS.Entity;
export import Kairo.ECS.SparseSet;
export import Kairo.ECS.Registry;
