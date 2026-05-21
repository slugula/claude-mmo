#pragma once

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"   // UiHoverState

namespace net { class NetworkClient; }

namespace ui {

// Called between Clay_BeginLayout() and Clay_EndLayout().
// Emits the full HUD panel (inventory / skills / equipment tabs).
void clayHudBuildLayout(const shared::PlayerState* player);

// Called after Clay_EndLayout().
// Resolves pointer-over hits, fires network actions, writes hover state,
// and opens any ImGui context-menu popups triggered this frame.
void clayHudHandleInput(const shared::PlayerState* player,
                        net::NetworkClient*         netc,
                        UiHoverState*               hover,
                        bool leftClicked,
                        bool rightClicked);

} // namespace ui
