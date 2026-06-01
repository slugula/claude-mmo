#pragma once

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"       // UiHoverState
#include "world/SpriteCache.hpp"

#include <glad/glad.h>

namespace net { class NetworkClient; }

namespace ui {

// Called between Clay_BeginLayout() and Clay_EndLayout().
// Emits the full HUD panel (inventory / skills / equipment tabs)
// and the minimap panel.
// sprites may be nullptr (slots fall back to text-only rendering).
// minimapTex = 0 means the minimap is not yet ready (panel not emitted).
void clayHudBuildLayout(const shared::PlayerState* player,
                        const SpriteCache*         sprites,
                        float mx, float my,
                        GLuint minimapTex = 0,
                        bool   bankOpen   = false);

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
