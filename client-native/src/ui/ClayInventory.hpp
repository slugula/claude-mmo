#pragma once

#include "shared/SharedTypes.hpp"

namespace ui {

// Declare and emit the Clay inventory panel for this frame.
// Call between Clay_BeginLayout() and Clay_EndLayout().
// `player` may be null (panel draws as empty slots).
void clayInventoryPanel(const shared::PlayerState* player);

} // namespace ui
