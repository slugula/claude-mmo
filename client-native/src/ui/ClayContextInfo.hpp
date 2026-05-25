#pragma once
// ClayContextInfo — fixed top-left screen overlay showing verb (white) +
// subject (orange) for the currently hovered world entity or UI slot.
// Call buildContextInfo() between Clay_BeginLayout and Clay_EndLayout.

namespace ui {

// verb   — e.g. "Chop down", "Attack", "Wield" (may be empty)
// subject — e.g. "Tree", "Chicken", "Bronze sword" (may be empty)
// Both strings must remain valid until after Clay_EndLayout for this frame.
void buildContextInfo(const char* verb, const char* subject);

} // namespace ui
