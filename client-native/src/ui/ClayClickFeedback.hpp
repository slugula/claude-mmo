#pragma once
// ClayClickFeedback — animated expanding circle at cursor click position.
// Rendered as a Clay ATTACH_TO_ROOT floating rounded-rect.
// Port of ClickFeedback.ts — 450ms lifetime, fades + scales up 60%.

namespace ui {

// Spawn a click feedback marker at screen position (x, y).
// colorType: 0 = yellow (walk), 1 = red (blocked/action).
void clickFeedbackSpawn(float x, float y, int colorType);

// Call between Clay_BeginLayout and Clay_EndLayout.
// dt is the frame delta in seconds.
void buildClickFeedback(float dt);

} // namespace ui
