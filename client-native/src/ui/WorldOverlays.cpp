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

  using Dur = std::chrono::steady_clock::duration;
  auto addSec = [](std::chrono::steady_clock::time_point t, float s) {
    return t + std::chrono::duration_cast<Dur>(std::chrono::duration<float>(s));
  };

  auto spawnSplat = [&](const std::string& id, int hitTick, int dmg,
                        glm::vec3 anchor) -> bool {
    if (hitTick <= 0) return false;
    auto it = seenHitTick_.find(id);
    if (it != seenHitTick_.end() && it->second == hitTick) return false;
    seenHitTick_[id] = hitTick;
    splats_.push_back({ anchor, dmg, now });
    return true;
  };

  if (localPlayer) {
    spawnSplat("__local__",
               localPlayer->lastHitTick,
               localPlayer->lastHitDamage,
               { static_cast<float>(localPlayer->tileX), 0.9f,
                 static_cast<float>(localPlayer->tileY) });

    // Drive the local HP bar fade timer.
    if (localPlayer->maxHp > 0 && localPlayer->hp < localPlayer->maxHp) {
      localHealthBarFadeUntil_ = addSec(now, kHealthBarFadeSec);
    }
  }

  for (const auto& n : npcs) {
    bool newHit = spawnSplat(n.id, n.lastHitTick, n.lastHitDamage,
               { static_cast<float>(n.tileX), 0.9f,
                 static_cast<float>(n.tileY) });
    // Refresh per-NPC health bar fade timer whenever a new hit lands.
    if (newHit) {
      npcHealthBarFadeUntil_[n.id] = addSec(now, kNpcBarFadeSec);
    }
  }
}

// ---------------------------------------------------------------------------
// Compute billboard bar pixel width that is invariant to camera rotation.
//
// A naive approach projects ±X world offsets, but when the camera looks from
// the side those offsets point into/out of the screen and the bar shrinks or
// expands with rotation.
//
// Instead we extract the camera RIGHT vector from the viewProj matrix.
// For a standard perspective projection P and view matrix V (column-major GLM):
//   viewProj[j][0] = (P[0][0]) * V[j][0]   (row 0 of viewProj)
// where V[j][0] = cameraRight[j]  (row 0 of V = camera right in world space)
// and P[0][0] = projXScale (x focal length).
//
// Since cameraRight is a unit vector:
//   projXScale = ||(viewProj[0][0], viewProj[1][0], viewProj[2][0])||
//   cameraRight = (viewProj[0][0], viewProj[1][0], viewProj[2][0]) / projXScale
//
// We then project anchor ± cameraRight * halfWorldW — these points are always
// perpendicular to the viewing direction, so the bar width is stable.
static float billboardBarW(const glm::mat4& vp, float wx, float anchorY, float wz,
                            float halfWorldW, int fbW, int fbH) {
  // Row 0 of viewProj = projXScale * cameraRight  (GLM col-major: [col][row])
  const glm::vec3 vpRow0(vp[0][0], vp[1][0], vp[2][0]);
  const float projXScale = glm::length(vpRow0);
  if (projXScale < 0.0001f) return 44.0f;
  const glm::vec3 camRight = vpRow0 / projXScale;

  const glm::vec3 anchor(wx, anchorY, wz);
  glm::vec2 pxL, pxR;
  const bool okL = worldToScreen(vp, anchor - camRight * halfWorldW, fbW, fbH, &pxL);
  const bool okR = worldToScreen(vp, anchor + camRight * halfWorldW, fbW, fbH, &pxR);
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
    drawBubble(localPlayer->wx, localPlayer->wy + 1.8f, localPlayer->wz,
               localPlayer->chatMessage, localPlayer->chatAlpha);
  }

  // ---- NPCs + remote players -----------------------------------------------
  for (const auto& e : entities) {
    if (e.showHpBar) {
      // NPC health bar: fades out kNpcBarFadeTailSec after last damage.
      float npcAlpha = 1.0f;
      if (!e.id.empty()) {
        auto fit = npcHealthBarFadeUntil_.find(e.id);
        if (fit == npcHealthBarFadeUntil_.end()) {
          npcAlpha = 0.0f;  // never been hit — don't show
        } else {
          const float secLeft =
              std::chrono::duration<float>(fit->second - now).count();
          npcAlpha = std::clamp(secLeft / kNpcBarFadeTailSec, 0.0f, 1.0f);
        }
      }
      if (npcAlpha > 0.0f)
        drawBar(e.wx, e.wy + 1.5f, e.wz, e.hp, e.maxHp, npcAlpha);
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

    // Billboard-scale the splat to represent a fixed ~0.18 world-unit radius.
    // Use the camera-right vector so rotation doesn't shrink/expand it.
    const float radius = billboardBarW(viewProj, pos.x, pos.y, pos.z,
                                       0.18f, fbWidth, fbHeight) * 0.5f;

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
