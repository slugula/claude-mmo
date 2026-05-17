#pragma once

#include "shared/SharedTypes.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

// Phase 8a world-space overlays drawn on the ImGui foreground draw list:
//   - health bars above the local player + any NPC currently being damaged
//     (or freshly damaged in the last few ticks)
//   - hit splats that spawn on lastHitTick changes and fade out
//
// Both consume StateMessage data each frame; no GL state of their own.

class WorldOverlays {
public:
  // Drives both detectors. `aspect` lets the projection match the main pass.
  void update(int currentTick,
              const std::optional<shared::PlayerState>& localPlayer,
              const std::vector<shared::NPCState>&      npcs);

  // Draw health bars + hit splats on the foreground draw list. The caller
  // supplies the same VP matrix used for world rendering, the framebuffer
  // size for the NDC -> pixel mapping, and a height functor (tx, ty) -> Y
  // so we can anchor bars above the tile geometry.
  template <typename HeightFn>
  void drawWithHeight(const glm::mat4& viewProj, int fbWidth, int fbHeight,
                      const std::optional<shared::PlayerState>& localPlayer,
                      const std::vector<shared::NPCState>&      npcs,
                      HeightFn&& getHeight);

private:
  struct Splat {
    glm::vec3                             worldAnchor;
    int                                   damage    = 0;
    std::chrono::steady_clock::time_point spawnedAt;
  };
  // Per-entity id -> last hit tick we recorded a splat for, so each tick's
  // damage only spawns one splat.
  std::unordered_map<std::string, int> seenHitTick_;
  std::vector<Splat>                   splats_;

  // Seeded on the first state message so we don't fire splats for ticks that
  // already existed when the session started.
  bool initialized_ = false;

  // Local player health bar fade: stays visible for kHealthBarFadeSec seconds
  // after HP returns to full. Zero when no bar should be shown.
  std::chrono::steady_clock::time_point localHealthBarFadeUntil_{};
  static constexpr float kHealthBarFadeSec = 10.0f;
};

// Convert a world position to pixel coordinates on the default framebuffer.
// Returns false when behind the camera or outside [0, 1] NDC clip.
bool worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                   int fbWidth, int fbHeight, glm::vec2* outPx);

}  // namespace ui

// ---- Inline template impl --------------------------------------------------

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace ui {

template <typename HeightFn>
inline void WorldOverlays::drawWithHeight(const glm::mat4& viewProj,
                                          int fbWidth, int fbHeight,
                                          const std::optional<shared::PlayerState>& localPlayer,
                                          const std::vector<shared::NPCState>&      npcs,
                                          HeightFn&& getHeight) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();

  auto drawHealthBar = [&](const glm::vec3& worldAnchor, int hp, int maxHp) {
    if (maxHp <= 0) return;
    glm::vec2 px;
    if (!worldToScreen(viewProj, worldAnchor, fbWidth, fbHeight, &px)) return;
    const float w = 44.0f, h = 6.0f;
    const float x = px.x - w * 0.5f;
    const float y = px.y - 4.0f;
    const float frac = std::clamp(static_cast<float>(hp) / static_cast<float>(maxHp),
                                  0.0f, 1.0f);
    dl->AddRectFilled(ImVec2(x,           y),
                      ImVec2(x + w,       y + h), IM_COL32(50, 0, 0, 220));
    dl->AddRectFilled(ImVec2(x,           y),
                      ImVec2(x + w * frac, y + h), IM_COL32(40, 200, 40, 230));
    dl->AddRect      (ImVec2(x,           y),
                      ImVec2(x + w,       y + h), IM_COL32(0, 0, 0, 255));
  };

  // Local player — only show when damaged; fade out kHealthBarFadeSec after
  // returning to full HP so it doesn't disappear with a jarring snap.
  if (localPlayer && localPlayer->maxHp > 0) {
    const auto nowBar = std::chrono::steady_clock::now();
    if (localPlayer->hp < localPlayer->maxHp) {
      // Actively damaged — keep the fade timer well ahead of now.
      localHealthBarFadeUntil_ = nowBar +
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<float>(kHealthBarFadeSec));
    }
    const float secLeft = std::chrono::duration<float>(localHealthBarFadeUntil_ - nowBar).count();
    if (secLeft > 0.0f) {
      const float alpha = std::clamp(secLeft / 1.5f, 0.0f, 1.0f);  // fade last 1.5s
      const float yWorld = getHeight(localPlayer->tileX, localPlayer->tileY);
      // Save/restore alpha via draw list — fade the whole bar.
      const ImU32 bgCol  = IM_COL32(50, 0, 0, static_cast<int>(220 * alpha));
      const ImU32 fgCol  = IM_COL32(40, 200, 40, static_cast<int>(230 * alpha));
      const ImU32 brdCol = IM_COL32(0, 0, 0, static_cast<int>(255 * alpha));
      glm::vec2 px;
      const glm::vec3 anchor {
        static_cast<float>(localPlayer->tileX),
        yWorld + 1.6f,    // lowered: sits just above the character head
        static_cast<float>(localPlayer->tileY)
      };
      if (worldToScreen(viewProj, anchor, fbWidth, fbHeight, &px)) {
        constexpr float w = 44.0f, h = 6.0f;
        const float x = px.x - w * 0.5f, y = px.y - 4.0f;
        const float frac = std::clamp(
            static_cast<float>(localPlayer->hp) / static_cast<float>(localPlayer->maxHp),
            0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(x,            y), ImVec2(x + w,         y + h), bgCol);
        dl->AddRectFilled(ImVec2(x,            y), ImVec2(x + w * frac,  y + h), fgCol);
        dl->AddRect      (ImVec2(x,            y), ImVec2(x + w,         y + h), brdCol);
      }
    }
  }
  // NPCs — only show a bar for NPCs that have taken damage.
  for (const auto& n : npcs) {
    if (n.dying || n.maxHp <= 0) continue;
    if (n.hp >= n.maxHp) continue;            // unwounded NPCs get no bar
    const float yWorld = getHeight(n.tileX, n.tileY);
    drawHealthBar({static_cast<float>(n.tileX),
                   yWorld + 1.6f,
                   static_cast<float>(n.tileY)},
                  n.hp, n.maxHp);
  }

  // Hit splats — fade out over ~1.2s, drift upward.
  const auto now = std::chrono::steady_clock::now();
  for (auto it = splats_.begin(); it != splats_.end();) {
    const float age = std::chrono::duration<float>(now - it->spawnedAt).count();
    if (age > 1.2f) { it = splats_.erase(it); continue; }
    glm::vec3 pos = it->worldAnchor;
    pos.y += 0.5f * age;                       // drift up
    glm::vec2 px;
    if (!worldToScreen(viewProj, pos, fbWidth, fbHeight, &px)) { ++it; continue; }
    const float alpha = std::clamp(1.0f - (age / 1.2f), 0.0f, 1.0f);
    const ImU32 fill  = (it->damage == 0)
                        ? IM_COL32(60, 60, 200, static_cast<int>(alpha * 230))
                        : IM_COL32(190, 30, 30, static_cast<int>(alpha * 230));
    const float radius = 11.0f;
    dl->AddCircleFilled(ImVec2(px.x, px.y), radius, fill, 16);
    dl->AddCircle      (ImVec2(px.x, px.y), radius, IM_COL32(0, 0, 0, static_cast<int>(alpha * 220)), 16);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", it->damage);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(px.x - ts.x * 0.5f, px.y - ts.y * 0.5f),
                IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)),
                buf);
    ++it;
  }
}

}  // namespace ui
