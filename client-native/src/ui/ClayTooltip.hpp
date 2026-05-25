#pragma once
// ClayTooltip — cursor-following tooltip rendered as a Clay ATTACH_TO_ROOT
// floating element at (mx+16, my+16).  Repositions to stay in-bounds near
// screen edges (flip left/up).
//
// Usage:
//   Call showTooltip(lines) each frame the tooltip should be visible.
//   buildTooltip(mx, my, screenW, screenH) is called inside clayFrame.

#include <string>
#include <vector>

namespace ui {

// One coloured segment of tooltip text.  Color components match Clay_Color
// convention: float values in the 0–255 range.
struct TipColor {
    float r = 255.f, g = 255.f, b = 255.f, a = 255.f;

    // Named constructors for the common palette.
    static constexpr TipColor White()  { return { 255, 255, 255, 255 }; }
    static constexpr TipColor Orange() { return { 255, 152,  31, 255 }; }
    static constexpr TipColor Gold()   { return { 255, 204,  68, 255 }; }
    static constexpr TipColor Grey()   { return { 136, 136, 136, 255 }; }
};

struct TooltipSeg {
    std::string text;
    TipColor    color = TipColor::White();
};

using TooltipLine = std::vector<TooltipSeg>;

// Show a structured (multi-line, multi-colour) tooltip.
// Call this every frame you want the tooltip to appear.
void showTooltip(std::vector<TooltipLine> lines);

// Convenience: single white line.
void showTooltip(const std::string& text);

// Suppress the tooltip for this frame.
void hideTooltip();

// Called between Clay_BeginLayout and Clay_EndLayout.
void buildTooltip(float mx, float my, float screenW, float screenH);

} // namespace ui
