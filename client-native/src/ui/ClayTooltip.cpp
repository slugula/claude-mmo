// ClayTooltip.cpp
// Cursor-following tooltip as a Clay ATTACH_TO_ROOT floating element.
// Visual: bg {10,5,0,230}, border {200,160,80,97} 1px, 11px font, 4×9 padding.
// Flips left/up when near screen edges.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayTooltip.hpp"

#include <algorithm>
#include <cstring>

namespace ui {

// ── State ─────────────────────────────────────────────────────────────────────
static bool        s_visible = false;
static std::string s_text;

static constexpr Clay_Color kBg     = {  10,   5,   0, 230 };
static constexpr Clay_Color kBorder = { 200, 160,  80,  97 };
static constexpr Clay_Color kText   = { 240, 206,  96, 255 };

// ── API ───────────────────────────────────────────────────────────────────────
void showTooltip(const std::string& text) {
    s_visible = true;
    s_text    = text;
}

void hideTooltip() {
    s_visible = false;
}

void buildTooltip(float mx, float my, float screenW, float screenH) {
    if (!s_visible || s_text.empty()) return;

    // Approximate text width: ~7px per char at 11px font
    constexpr float kPadX    = 8.f;
    constexpr float kPadY    = 4.f;
    constexpr float kCharW   = 7.f;
    constexpr float kFontH   = 11.f;
    constexpr float kOffset  = 16.f;

    float ttW = static_cast<float>(s_text.size()) * kCharW + kPadX * 2.f;
    float ttH = kFontH + kPadY * 2.f;

    float ox = mx + kOffset;
    float oy = my + kOffset;

    // Flip left near right edge
    if (ox + ttW > screenW - 4.f) ox = mx - ttW - 4.f;
    // Flip up near bottom edge
    if (oy + ttH > screenH - 4.f) oy = my - ttH - 4.f;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    // Non-literal string: build Clay_String manually
    Clay_String cs = { false, static_cast<int>(s_text.size()), s_text.c_str() };

    CLAY(CLAY_ID("TooltipAnchor"), {
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 100,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("TooltipBox"), {
            .layout = {
                .sizing   = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                .padding  = { (uint16_t)kPadX, (uint16_t)kPadX,
                              (uint16_t)kPadY, (uint16_t)kPadY },
                .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(2),
            .border = {
                .color = kBorder,
                .width = CLAY_BORDER_ALL(1),
            }
        }) {
            CLAY_TEXT(cs, CLAY_TEXT_CONFIG({
                .textColor = kText,
                .fontSize  = 11,
            }));
        }
    }

    // Reset for next frame
    s_visible = false;
}

} // namespace ui
