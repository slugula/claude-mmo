#pragma once

#include "world/GltfModel.hpp"

#include <filesystem>
#include <optional>

namespace world {

// Load a .glb (or .gltf) file from disk. Returns nullopt on read or parse
// errors (and logs the failure to stderr).
std::optional<GltfModel> loadGlb(const std::filesystem::path& path);

}  // namespace world
