#pragma once

// SkeletonConfig — the SINGLE place that maps logical attachment "sockets" to
// the actual joint names of the current player model.
//
// Why this exists: the player model (player.glb) is a temporary placeholder and
// WILL be replaced. Every other system refers to sockets logically
// ("weapon_main"), never to a raw joint name. When the model changes, update the
// joint names HERE (and re-tune item grip values in the editor) — no other code
// changes. If a socket's joint isn't found in the loaded model, callers log a
// warning and skip the attachment rather than crashing.
//
// Current model: UE-style 65-joint humanoid. Right hand = "hand_r".

#include <string>

namespace world {

// Logical socket names (stable across model swaps).
inline constexpr const char* kSocketWeaponMain = "weapon_main";

// Resolve a logical socket → joint name for the CURRENT model.
// Returns empty string for an unknown socket.
inline std::string resolveSocketJoint(const std::string& socket) {
  if (socket == kSocketWeaponMain) return "hand_r";
  return "";
}

}  // namespace world
