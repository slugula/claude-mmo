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
//   - health bars above players / NPCs (billboard-sized, world-space)
//   - overhead chat bubbles (yellow text with dark background)
//   - hit splats that spawn on lastHitTick changes and fade out
//
// All overlays use billboard sizing: two world-space edge points are projected
// to screen each frame so the bar/bubble scales naturally with camera distance.

class WorldOverlays {
public:
  // Pre-computed per-entity data built by App each frame from the
  // interpolation loops (float world position, not integer tile coords).
  struct OverlayEntry {
    float       wx        = 0.0f;   // interpolated world X
    float       wy        = 0.0f;   // terrain Y at this position
    float       wz        = 0.0f;   // interpolated world Z
    int         hp        = 0;
    int         maxHp     = 0;
    bool        showHpBar = false;  // explicit: draw a health bar
    std::string chatMessage;        // empty = no bubble
    float       chatAlpha = 0.0f;   // 0 = hidden, 1 = fully visible
  };

  // Drives hit-splat detection. `aspect` lets the projection match the main
  // pass.  Should be called once per frame after receiving fresh state.
  void update(int currentTick,
              const std::optional<shared::PlayerState>& localPlayer,
              const std::vector<shared::NPCState>&      npcs);

  // Draw all overlays on the ImGui foreground draw list.
  //
  // localPlayer: pre-built entry for the local player at its interpolated
  //              world position; nullptr if not yet logged in.
  // entities:    one entry per NPC + one per remote player (combined).
  //
  // Health bars scale with camera distance (billboard projection).
  // Local player bar fades out kHealthBarFadeSec seconds after HP returns
  // to max; the fade timer is maintained internally by update().
  void draw(const glm::mat4& viewProj, int fbWidth, int fbHeight,
            const OverlayEntry* localPlayer,
            const std::vector<OverlayEntry>& entities);

private:
  struct Splat {
    glm::vec3                             worldAnchor;
    int                                   damage    = 0;
    std::chrono::steady_clock::time_point spawnedAt;
  };
  std::unordered_map<std::string, int> seenHitTick_;
  std::vector<Splat>                   splats_;
  bool initialized_ = false;

  // Local player health bar fade: stays visible for kHealthBarFadeSec seconds
  // after HP returns to full.
  std::chrono::steady_clock::time_point localHealthBarFadeUntil_{};
  static constexpr float kHealthBarFadeSec = 10.0f;
};

// Convert a world position to pixel coordinates on the default framebuffer.
// Returns false when behind the camera or outside [-1, 1] NDC depth.
bool worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                   int fbWidth, int fbHeight, glm::vec2* outPx);

}  // namespace ui
