#include "ui/WorldOverlays.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace ui {

// ---------------------------------------------------------------------------
bool worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                   int fbWidth, int fbHeight, glm::vec2* outPx) {
  if (!outPx || fbWidth <= 0 || fbHeight <= 0) return false;
  glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0001f) return false;                    // behind camera
  const glm::vec3 ndc { clip.x / clip.w, clip.y / clip.w, clip.z / clip.w };
  if (ndc.z < -1.0f || ndc.z > 1.0f) return false;       // outside near/far
  outPx->x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(fbWidth);
  outPx->y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(fbHeight);
  return true;
}

// ---------------------------------------------------------------------------
void WorldOverlays::update(int /*currentTick*/,
                           const std::optional<shared::PlayerState>& localPlayer,
                           const std::vector<shared::NPCState>&      npcs) {
  const auto now = std::chrono::steady_clock::now();

  // On the very first state message, seed seenHitTick_ with every entity's
  // current lastHitTick so we don't fire splats for damage that already
  // happened before this session started.
  if (!initialized_) {
    initialized_ = true;
    if (localPlayer && localPlayer->lastHitTick > 0)
      seenHitTick_["__local__"] = localPlayer->lastHitTick;
    for (const auto& n : npcs) {
      if (n.lastHitTick > 0)
        seenHitTick_[n.id] = n.lastHitTick;
    }
    return;
  }

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
               { static_cast<float>(localPlayer->tileX), 1.5f,
                 static_cast<float>(localPlayer->tileY) });

    // Drive the local HP bar fade timer.
    if (localPlayer->maxHp > 0 && localPlayer->hp < localPlayer->maxHp) {
      localHealthBarFadeUntil_ = now +
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<float>(kHealthBarFadeSec));
    }
  }

  for (const auto& n : npcs) {
    spawnSplat(n.id, n.lastHitTick, n.lastHitDamage,
               { static_cast<float>(n.tileX), 1.2f,
                 static_cast<float>(n.tileY) });
  }
}

// ---------------------------------------------------------------------------
// Compute billboard bar pixel width by projecting two world-edge points.
// Returns a clamped minimum so bars are always readable up close.
static float billboardBarW(const glm::mat4& vp, float wx, float wy, float wz,
                            float halfWorldW, int fbW, int fbH) {
  glm::vec2 pxL, pxR;
  const bool okL = worldToScreen(vp, {wx - halfWorldW, wy, wz}, fbW, fbH, &pxL);
  const bool okR = worldToScreen(vp, {wx + halfWorldW, wy, wz}, fbW, fbH, &pxR);
  if (okL && okR) return std::max(14.0f, std::abs(pxR.x - pxL.x));
  return 44.0f;  // fallback
}

// ---------------------------------------------------------------------------
void WorldOverlays::draw(const glm::mat4& viewProj, int fbWidth, int fbHeight,
                         const OverlayEntry* localPlayer,
                         const std::vector<OverlayEntry>& entities) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const auto  now = std::chrono::steady_clock::now();

  // ---- Helper: draw one health bar at a world anchor ----------------------
  auto drawBar = [&](float wx, float barAnchorY, float wz,
                     int hp, int maxHp, float alpha) {
    if (maxHp <= 0 || alpha <= 0.0f) return;
    glm::vec2 px;
    if (!worldToScreen(viewProj, {wx, barAnchorY, wz}, fbWidth, fbHeight, &px))
      return;

    const float w = billboardBarW(viewProj, wx, barAnchorY, wz,
                                  0.45f, fbWidth, fbHeight);
    const float h = std::max(3.0f, w / 8.0f);
    const float x = px.x - w * 0.5f;
    const float y = px.y - 4.0f;
    const float frac = std::clamp(static_cast<float>(hp) / static_cast<float>(maxHp),
                                  0.0f, 1.0f);

    const ImU32 bgCol  = IM_COL32(50,  0,  0, static_cast<int>(220 * alpha));
    const ImU32 fgCol  = IM_COL32(40, 200, 40, static_cast<int>(230 * alpha));
    const ImU32 brdCol = IM_COL32( 0,   0,  0, static_cast<int>(255 * alpha));

    dl->AddRectFilled(ImVec2(x,           y), ImVec2(x + w,        y + h), bgCol);
    dl->AddRectFilled(ImVec2(x,           y), ImVec2(x + w * frac, y + h), fgCol);
    dl->AddRect      (ImVec2(x,           y), ImVec2(x + w,        y + h), brdCol);
  };

  // ---- Helper: draw one chat bubble at a world anchor ---------------------
  auto drawBubble = [&](float wx, float bubbleAnchorY, float wz,
                        const std::string& msg, float alpha) {
    if (msg.empty() || alpha <= 0.0f) return;
    glm::vec2 px;
    if (!worldToScreen(viewProj, {wx, bubbleAnchorY, wz}, fbWidth, fbHeight, &px))
      return;

    const ImVec2 ts   = ImGui::CalcTextSize(msg.c_str());
    const float padX  = 4.0f, padY = 2.0f;
    dl->AddRectFilled(
        ImVec2(px.x - ts.x * 0.5f - padX, px.y - ts.y - padY),
        ImVec2(px.x + ts.x * 0.5f + padX, px.y + padY),
        IM_COL32(0, 0, 0, static_cast<int>(140 * alpha)), 4.0f);
    dl->AddText(
        ImVec2(px.x - ts.x * 0.5f, px.y - ts.y),
        IM_COL32(255, 255, 0, static_cast<int>(255 * alpha)),
        msg.c_str());
  };

  // ---- Local player — HP bar fades kHealthBarFadeSec after full HP --------
  if (localPlayer) {
    const float secLeft =
        std::chrono::duration<float>(localHealthBarFadeUntil_ - now).count();
    const float alpha = std::clamp(secLeft / 1.5f, 0.0f, 1.0f);

    if (localPlayer->showHpBar && alpha > 0.0f) {
      // Anchor: 2.4 world units above terrain — clears the player head
      drawBar(localPlayer->wx, localPlayer->wy + 2.4f, localPlayer->wz,
              localPlayer->hp, localPlayer->maxHp, alpha);
    }
    drawBubble(localPlayer->wx, localPlayer->wy + 2.8f, localPlayer->wz,
               localPlayer->chatMessage, localPlayer->chatAlpha);
  }

  // ---- NPCs + remote players -----------------------------------------------
  for (const auto& e : entities) {
    if (e.showHpBar) {
      // NPC anchor: 1.5 world units above terrain clears the humanoid head
      drawBar(e.wx, e.wy + 1.5f, e.wz, e.hp, e.maxHp, 1.0f);
    }
    drawBubble(e.wx, e.wy + 1.8f, e.wz, e.chatMessage, e.chatAlpha);
  }

  // ---- Hit splats — fade over ~1.2s, drift upward -------------------------
  for (auto it = splats_.begin(); it != splats_.end();) {
    const float age = std::chrono::duration<float>(now - it->spawnedAt).count();
    if (age > 1.2f) { it = splats_.erase(it); continue; }

    glm::vec3 pos = it->worldAnchor;
    pos.y += 0.5f * age;                       // drift up
    glm::vec2 px;
    if (!worldToScreen(viewProj, pos, fbWidth, fbHeight, &px)) { ++it; continue; }

    const float alpha = std::clamp(1.0f - (age / 1.2f), 0.0f, 1.0f);
    const ImU32 fill  = (it->damage == 0)
                        ? IM_COL32( 60,  60, 200, static_cast<int>(alpha * 230))
                        : IM_COL32(190,  30,  30, static_cast<int>(alpha * 230));

    // Billboard-scale the splat radius so it doesn't shrink to nothing far away
    glm::vec2 pxEdge;
    float radius = 11.0f;
    if (worldToScreen(viewProj, {pos.x + 0.2f, pos.y, pos.z}, fbWidth, fbHeight, &pxEdge))
      radius = std::clamp(std::abs(pxEdge.x - px.x) / 0.2f * 0.2f, 8.0f, 18.0f);

    dl->AddCircleFilled(ImVec2(px.x, px.y), radius, fill, 16);
    dl->AddCircle      (ImVec2(px.x, px.y), radius,
                        IM_COL32(0, 0, 0, static_cast<int>(alpha * 220)), 16);

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", it->damage);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(px.x - ts.x * 0.5f, px.y - ts.y * 0.5f),
                IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)), buf);
    ++it;
  }
}

}  // namespace ui
