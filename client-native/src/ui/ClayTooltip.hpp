#pragma once
// ClayTooltip — cursor-following tooltip rendered as a Clay ATTACH_TO_ROOT
// floating element at (mx+16, my+16).  Repositions to stay in-bounds near
// screen edges (flip left/up).
//
// Usage: call showTooltip(text) each frame the tooltip should be visible.
//        Call hideTooltip() to suppress it.
//        buildTooltip(mx, my, screenW, screenH) is called by clayFrame.

#include <string>

namespace ui {

// Show a single-line tooltip with the given text on the next buildTooltip call.
// Call this every frame you want the tooltip to appear.
void showTooltip(const std::string& text);

// Suppress the tooltip for this frame.
void hideTooltip();

// Called between Clay_BeginLayout and Clay_EndLayout.
void buildTooltip(float mx, float my, float screenW, float screenH);

} // namespace ui
