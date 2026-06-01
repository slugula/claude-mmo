#pragma once
// ClayBankPanel — Clay-based bank window.
// Rendered as a CLAY_ATTACH_TO_ROOT floating panel centred on screen.
// Right-click menus use the shared ClayContextMenu singleton.
// Call buildBankPanel() inside Clay_BeginLayout / Clay_EndLayout.
// Check bankWantsClose() AFTER clayFrame to send closeBank and clear bankOpen_.

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"        // UiHoverState
#include "world/SpriteCache.hpp"

namespace net { class NetworkClient; }

namespace ui {

// Build the bank layout element.
// bankOpen: pass true only when the bank should be shown.
// leftClicked / rightClicked: per-frame button states.
// hover: written with "Withdraw-1 {Item}" info when a bank slot is hovered.
void buildBankPanel(float screenW, float screenH,
                    const shared::PlayerState* player,
                    net::NetworkClient* netc,
                    const SpriteCache* sprites,
                    bool bankOpen,
                    bool leftClicked,
                    bool rightClicked,
                    bool mouseDown,
                    float mx, float my,
                    UiHoverState* hover);

// Returns true on the first frame the user closes the bank
// (clicked outside the panel). Reset automatically after being read.
bool bankWantsClose();

// ── Bank window position persistence ──────────────────────────────────────────
// The window is draggable by its header; App persists the position via settings.
// getPosition returns false until the user has moved/positioned the window.
bool bankPanelGetPosition(float& x, float& y);
void bankPanelSetPosition(float x, float y);
// True for one read after the user finishes dragging — App saves settings then.
bool bankPanelPositionChanged();

} // namespace ui
