// ClayTooltip.cpp
// Cursor-following tooltip as a Clay ATTACH_TO_ROOT floating element.
// Visual: bg {10,5,0,230}, border {200,160,80,200} 1px, 11px font, 4×8 padding.
// Supports multi-line, multi-colour text segments.
// Flips left/up when near screen edges.
//
// Shell+content pattern (same as ClayContextMenu):
//   Shell  — FIXED size, bg + border, empty children.  zIndex 100.
//   Content — FIXED width + FIT height, no border, text rows.  zIndex 101.

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
static bool                    s_visible = false;
static std::vector<TooltipLine> s_lines;

static constexpr Clay_Color kBg     = {  10,   5,   0, 230 };
static constexpr Clay_Color kBorder = { 200, 160,  80, 200 };

// Scratch buffer for Clay_String char pointers (must live until render ends).
static char  s_strBuf[4096];
static int   s_strOff = 0;

static Clay_String tcs(const std::string& s) {
    int len = static_cast<int>(s.size());
    if (s_strOff + len + 1 > static_cast<int>(sizeof(s_strBuf))) {
        len = static_cast<int>(sizeof(s_strBuf)) - s_strOff - 1;
        if (len <= 0) return { false, 0, "" };
    }
    char* dst = s_strBuf + s_strOff;
    std::memcpy(dst, s.c_str(), len);
    dst[len] = '\0';
    s_strOff += len + 1;
    return { false, len, dst };
}

// ── API ───────────────────────────────────────────────────────────────────────
void showTooltip(std::vector<TooltipLine> lines) {
    s_visible = true;
    s_lines   = std::move(lines);
}

void showTooltip(const std::string& text) {
    s_visible = true;
    s_lines   = { { TooltipSeg{ text, TipColor::White() } } };
}

void hideTooltip() {
    s_visible = false;
}

void buildTooltip(float mx, float my, float screenW, float screenH) {
    if (!s_visible || s_lines.empty()) return;

    s_strOff = 0;  // reset scratch

    constexpr float kPadX    = 8.f;
    constexpr float kPadY    = 4.f;
    constexpr float kCharW   = 7.5f;   // approximate char width
    constexpr float kFontH   = 14.f;
    constexpr float kLineGap = 2.f;
    constexpr float kOffset  = 16.f;

    // Estimate tooltip size (same calculation drives both shell and content).
    float maxLineW = 0.f;
    for (const auto& line : s_lines) {
        float lineW = 0.f;
        for (const auto& seg : line)
            lineW += static_cast<float>(seg.text.size()) * kCharW;
        maxLineW = std::max(maxLineW, lineW);
    }
    float numLines = static_cast<float>(s_lines.size());
    float ttW = maxLineW + kPadX * 2.f;
    float ttH = numLines * kFontH + (numLines - 1.f) * kLineGap + kPadY * 2.f;

    float ox = mx + kOffset;
    float oy = my + kOffset;
    if (ox + ttW > screenW - 4.f) ox = mx - ttW - 4.f;
    if (oy + ttH > screenH - 4.f) oy = my - ttH - 4.f;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    // ── Shell — background + border only, no children ────────────────────────
    CLAY(CLAY_ID("TooltipShell"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(ttW), CLAY_SIZING_FIXED(ttH) },
        },
        .backgroundColor = kBg,
        .cornerRadius    = CLAY_CORNER_RADIUS(2),
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 100,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        },
        .border = {
            .color = kBorder,
            .width = CLAY_BORDER_ALL(1),
        },
    }) {}

    // ── Content — separate floating element on top, no border ────────────────
    CLAY(CLAY_ID("TooltipBox"), {
        .layout = {
            .sizing          = { CLAY_SIZING_FIXED(ttW), CLAY_SIZING_FIT(0) },
            .padding         = { (uint16_t)kPadX, (uint16_t)kPadX,
                                 (uint16_t)kPadY, (uint16_t)kPadY },
            .childGap        = 0,
            .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_TOP },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 101,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        },
    }) {
        for (int li = 0; li < static_cast<int>(s_lines.size()); ++li) {
            const auto& line = s_lines[li];
            // kLineGap applied as top padding on every row except the first.
            uint16_t rowTopPad = (li > 0) ? static_cast<uint16_t>(kLineGap) : 0;
            CLAY(CLAY_IDI("TipLine", li), {
                .layout = {
                    .sizing          = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                    .padding         = { 0, 0, rowTopPad, 0 },
                    .childGap        = 0,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                }
            }) {
                for (int si = 0; si < static_cast<int>(line.size()); ++si) {
                    const auto& seg = line[si];
                    if (seg.text.empty()) continue;
                    Clay_Color col = { seg.color.r, seg.color.g,
                                       seg.color.b, seg.color.a };
                    CLAY_TEXT(tcs(seg.text), CLAY_TEXT_CONFIG({
                        .textColor = col,
                        .fontSize  = 0,
                    }));
                }
            }
        }
    }

    // Reset for next frame
    s_visible = false;
}

} // namespace ui
