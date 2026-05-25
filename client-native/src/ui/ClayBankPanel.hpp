#pragma once
// ClayBankPanel — Clay-based bank window.
// Rendered as a CLAY_ATTACH_TO_ROOT floating panel centred on screen.
// Right-click menus use the shared ClayContextMenu singleton.
// Call buildBankPanel() inside Clay_BeginLayout / Clay_EndLayout.
// Check bankWantsClose() AFTER clayFrame to send closeBank and clear bankOpen_.

#include "shared/SharedTypes.hpp"
#include "world/SpriteCache.hpp"

namespace net { class NetworkClient; }

namespace ui {

// Build the bank layout element.
// bankOpen: pass true only when the bank should be shown.
// leftClicked / rightClicked: per-frame button states.
void buildBankPanel(float screenW, float screenH,
                    const shared::PlayerState* player,
                    net::NetworkClient* netc,
                    const SpriteCache* sprites,
                    bool bankOpen,
                    bool leftClicked,
                    bool rightClicked);

// Returns true on the first frame the user closes the bank
// (clicked outside the panel). Reset automatically after being read.
bool bankWantsClose();

} // namespace ui
