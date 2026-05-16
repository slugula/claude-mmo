#include "ui/WorldOverlays.hpp"

#include <chrono>

namespace ui {

bool worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                   int fbWidth, int fbHeight, glm::vec2* outPx) {
  if (!outPx || fbWidth <= 0 || fbHeight <= 0) return false;
  glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0001f) return false;                    // behind camera
  const glm::vec3 ndc { clip.x / clip.w, clip.y / clip.w, clip.z / clip.w };
  if (ndc.z < -1.0f || ndc.z > 1.0f) return false;        // outside near/far
  // NDC (-1..1) -> pixel coords with y flipped (ImGui origin = top-left)
  outPx->x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(fbWidth);
  outPx->y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(fbHeight);
  return true;
}

void WorldOverlays::update(int /*currentTick*/,
                           const std::optional<shared::PlayerState>& localPlayer,
                           const std::vector<shared::NPCState>&      npcs) {
  const auto now = std::chrono::steady_clock::now();

  auto spawnSplat = [&](const std::string& id, int hitTick, int dmg,
                        glm::vec3 anchor) {
    if (hitTick <= 0) return;
    auto it = seenHitTick_.find(id);
    if (it != seenHitTick_.end() && it->second == hitTick) return;
    seenHitTick_[id] = hitTick;
    splats_.push_back({ anchor, dmg, now });
  };

  if (localPlayer) {
    spawnSplat("__local__",
               localPlayer->lastHitTick,
               localPlayer->lastHitDamage,
               { static_cast<float>(localPlayer->tileX),
                 1.5f,
                 static_cast<float>(localPlayer->tileY) });
  }
  for (const auto& n : npcs) {
    spawnSplat(n.id,
               n.lastHitTick,
               n.lastHitDamage,
               { static_cast<float>(n.tileX),
                 1.2f,
                 static_cast<float>(n.tileY) });
  }
}

}  // namespace ui
