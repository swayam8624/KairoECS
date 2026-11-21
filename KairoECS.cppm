// TEACHING FILE GUIDE: This umbrella C++23 module re-exports Entity, SparseSet and Registry so application systems can import Kairo.ECS. The three modules intentionally separate handle identity, dense per-type storage and the registry API; the umbrella contains no runtime behavior.
export module Kairo.ECS;

export import Kairo.ECS.Entity;
export import Kairo.ECS.SparseSet;
export import Kairo.ECS.Registry;
