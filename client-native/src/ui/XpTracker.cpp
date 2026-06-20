#include "ui/XpTracker.hpp"
#include "ui/SkillXp.hpp"
#include "ui/NameRegistry.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace ui {

// ── Tuning ──────────────────────────────────────────────────────────────────
static constexpr float kDropLife = 2.4f;   // seconds a drop lives (slower rise)
static constexpr float kIconNative = 32.0f; // sprite native size — draw 1:1 to stay crisp

static float easeOutCubic(float t) { t = 1.0f - t; return 1.0f - t * t * t; }

// "1,234,567"
static std::string commafy(long long v) {
    char raw[32];
    std::snprintf(raw, sizeof(raw), "%lld", v);
    std::string s(raw), out;
    int c = 0;
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
        out.push_back(s[i]);
        if (++c % 3 == 0 && i > 0) out.push_back(',');
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Draw a skill icon (or a colored letter fallback) at p0..p1 with alpha.
static void drawIcon(ImDrawList* dl, const SpriteCache* sprites,
                     const std::string& skillId, ImVec2 p0, ImVec2 p1, int alpha) {
    if (sprites && sprites->has(skillId)) {
        dl->AddImage((ImTextureID)(uintptr_t)sprites->get(skillId), p0, p1,
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
        return;
    }
    const SkillColor c = skillColor(skillId);
    dl->AddRectFilled(p0, p1, IM_COL32(c.r, c.g, c.b, alpha), 4.0f);
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, alpha), 4.0f);
    if (!skillId.empty()) {
        char letter[2] = { static_cast<char>(std::toupper(skillId[0])), 0 };
        const float fs = (p1.y - p0.y) * 0.7f;
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, -1.f, letter);
        ImVec2 tp{ (p0.x + p1.x) * 0.5f - ts.x * 0.5f, (p0.y + p1.y) * 0.5f - ts.y * 0.5f };
        dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(255, 255, 255, alpha), letter);
    }
}

void XpTracker::pushGain(const std::string& skillId, int amount, double totalXp, int level) {
    if (amount <= 0) return;
    const bool switched = (skillId != skillId_);
    skillId_   = skillId;
    level_     = level;
    targetXp_  = totalXp;
    targetProg_ = levelProgress(totalXp, level);
    if (switched || !active_) {
        // Start the count-up from just before this gain so the delta animates.
        shownXp_   = std::max(0.0, totalXp - amount);
        shownProg_ = levelProgress(shownXp_, level);
    }
    active_    = true;
    sinceGain_ = 0.0f;
    pulse_     = 1.0f;

    // Stagger overlapping drops so rapid gains don't perfectly stack.
    float slot = 0.0f;
    for (const auto& d : drops_) if (d.age < 0.35f) slot += 1.0f;
    drops_.push_back({ skillId, amount, 0.0f, kDropLife, slot });
}

void XpTracker::render(const SpriteCache* sprites, float screenW, float screenH,
                       float dt, float uiScale) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = (uiScale > 0.1f) ? uiScale : 1.0f;
    const float cx = screenW * 0.5f;

    // ── Advance tracker animation ────────────────────────────────────────────
    sinceGain_ += dt;
    const float k = std::min(1.0f, dt * 9.0f);   // easing factor toward targets
    shownXp_   += (targetXp_   - shownXp_)   * k;
    if (std::abs(targetXp_ - shownXp_) < 0.5) shownXp_ = targetXp_;
    shownProg_ += (targetProg_ - shownProg_) * k;
    pulse_ = std::max(0.0f, pulse_ - dt * 2.5f);

    // ── Tracker geometry (top-center, raised up) ─────────────────────────────
    const float boxW = 172.0f * s;
    const float iconSz = kIconNative * s;           // draw the 32px sprite 1:1
    const float pad  = 6.0f  * s;
    const float rowH = iconSz + pad;                 // icon + number row
    const float barH = 9.0f  * s;                    // progress bar (a touch taller)
    const float boxH = rowH + barH + pad * 1.5f;
    const float boxX = cx - boxW * 0.5f;
    const float boxY = 22.0f * s;                    // higher up the screen
    const SkillColor sc = skillColor(skillId_);

    // ── XP drops (drawn FIRST so the tracker box occludes them) ──────────────
    // Rise from just above center up toward the tracker; once they slide behind
    // the (opaque) box they're hidden. easeOut decelerates as they near the box.
    const float startY  = screenH * 0.46f;
    const float targetY = boxY + boxH * 0.5f;        // settle inside the box → occluded
    for (auto& d : drops_) {
        d.age += dt;
        const float t = std::clamp(d.age / d.life, 0.0f, 1.0f);

        // Fade in only; disappearance is by occlusion behind the tracker.
        float alpha = (t < 0.12f) ? (t / 0.12f) : 1.0f;
        const int a = static_cast<int>(alpha * 255.0f);
        if (a <= 0) continue;

        const float y = startY - d.slot * (iconSz * 0.6f)
                      - (startY - targetY) * easeOutCubic(t);

        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", d.amount);   // no leading '+'
        const float fs = 13.0f * s;   // native pixel-font size — crisp, matches the UI
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, -1.f, buf);
        const float gap = 5.0f * s;
        const float totalW = iconSz + gap + ts.x;
        const float x0 = cx - totalW * 0.5f;

        // Full-size icon + white number (shadowed for legibility over the world).
        drawIcon(dl, sprites, d.skillId, ImVec2(x0, y - iconSz * 0.5f),
                 ImVec2(x0 + iconSz, y + iconSz * 0.5f), a);
        const ImVec2 tp{ x0 + iconSz + gap, y - ts.y * 0.5f };
        dl->AddText(ImGui::GetFont(), fs, ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, a), buf);
        dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(255, 255, 255, a), buf);
    }
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                 [](const Drop& d) { return d.age >= d.life; }), drops_.end());

    // ── XP tracker box (drawn on top; stays once active — no idle fade) ───────
    if (active_) {
        // Pulse: a soft outer glow + border brightening.
        if (pulse_ > 0.001f) {
            const int ga = static_cast<int>(pulse_ * 120.0f);
            const float g = 3.0f * s * pulse_;
            dl->AddRect(ImVec2(boxX - g, boxY - g), ImVec2(boxX + boxW + g, boxY + boxH + g),
                        IM_COL32(sc.r, sc.g, sc.b, ga), 4.0f * s, 0, 2.0f * s);
        }
        // Opaque box (so drops vanish cleanly behind it) + 1px border.
        dl->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH),
                          IM_COL32(18, 14, 8, 255), 3.0f * s);
        const int border = static_cast<int>(std::clamp(0.6f + 0.4f * pulse_, 0.f, 1.f) * 255.0f);
        dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH),
                    IM_COL32(sc.r, sc.g, sc.b, border), 3.0f * s, 0, 1.0f);

        // Icon (left, full size).
        const ImVec2 ip0{ boxX + pad, boxY + pad * 0.5f };
        const ImVec2 ip1{ ip0.x + iconSz, ip0.y + iconSz };
        drawIcon(dl, sprites, skillId_, ip0, ip1, 255);

        // Total XP (right-aligned in the row).
        const std::string xpStr = commafy(static_cast<long long>(shownXp_ + 0.5));
        const float fs = 13.0f * s;   // native pixel-font size — crisp, matches the UI
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, -1.f, xpStr.c_str());
        ImVec2 tp{ boxX + boxW - pad - ts.x, boxY + pad * 0.5f + (iconSz - ts.y) * 0.5f };
        dl->AddText(ImGui::GetFont(), fs, ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 255), xpStr.c_str());
        dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(255, 255, 255, 255), xpStr.c_str());

        // Progress bar (solid fill, no rounding/gradient).
        const ImVec2 bp0{ boxX + pad, boxY + rowH + pad * 0.25f };
        const ImVec2 bp1{ boxX + boxW - pad, bp0.y + barH };
        dl->AddRectFilled(bp0, bp1, IM_COL32(0, 0, 0, 200), 0.0f);
        const float fillW = (bp1.x - bp0.x) * std::clamp(shownProg_, 0.0f, 1.0f);
        if (fillW > 1.0f)
            dl->AddRectFilled(bp0, ImVec2(bp0.x + fillW, bp1.y),
                              IM_COL32(sc.r, sc.g, sc.b, 255), 0.0f);
        dl->AddRect(bp0, bp1, IM_COL32(0, 0, 0, 160), 0.0f);
    }
}

} // namespace ui
