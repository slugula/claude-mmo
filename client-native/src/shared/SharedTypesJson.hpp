#pragma once

// Glaze JSON adapters for the types in SharedTypes.hpp.
//
// All structs use plain aggregate reflection (glaze handles them automatically).
// Only the two string-backed enums need explicit metadata so that JSON values
// like "grass" / "tree" round-trip correctly.

#include "shared/SharedTypes.hpp"

#include <glaze/glaze.hpp>

template <>
struct glz::meta<shared::TileType> {
  using enum shared::TileType;
  static constexpr auto value = enumerate(
    "grass", grass,
    "dirt",  dirt,
    "stone", stone,
    "water", water,
    "cliff", cliff,
    "wall",  wall,
    "door",  door);
};

template <>
struct glz::meta<shared::ObstacleType> {
  using enum shared::ObstacleType;
  static constexpr auto value = enumerate(
    "tree",         tree,
    "rock",         rock,
    "chest",        chest,
    "fishing_spot", fishing_spot,
    "none",         none);
};
