#pragma once

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"       // UiHoverState
#include "world/SpriteCache.hpp"

namespace net { class NetworkClient; }

namespace ui {

// Called between Clay_BeginLayout() and Clay_EndLayout().
// Emits the full HUD panel (inventory / skills / equipment tabs).
// sprites may be nullptr (slots fall back to text-only rendering).
void clayHudBuildLayout(const shared::PlayerState* player,
                        const SpriteCache*         sprites,
                        float mx, float my);

// Called after Clay_EndLayout().
// Resolves pointer-over hits, fires network actions, writes hover state,
// and opens any ImGui context-menu popups triggered this frame.
void clayHudHandleInput(const shared::PlayerState* player,
                        net::NetworkClient*         netc,
                        UiHoverState*               hover,
                        bool leftClicked,
                        bool rightClicked,
                        bool mouseDown,
                        float mx, float my);

} // namespace ui
