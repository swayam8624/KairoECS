// TEACHING MAP — KairoECS.cppm
// Exports the Entity, SparseSet, and Registry modules through one public C++23 facade.
// Gameplay clients import it; ECS itself owns neither GPU rendering nor scene persistence.
// See READING_ORDER.md for the complete module sequence, API interactions, and tests.

export module Kairo.ECS;

export import Kairo.ECS.Entity;
export import Kairo.ECS.SparseSet;
export import Kairo.ECS.Registry;
