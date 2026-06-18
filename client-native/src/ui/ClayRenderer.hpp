#pragma once

// Clay types are fully hidden behind this header.
// App.cpp never needs to include clay.h.

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"       // UiHoverState
#include "world/SpriteCache.hpp"

namespace net { class NetworkClient; }

namespace ui {

// UI scale: the whole Clay layout is authored in logical pixels and scaled by
// this factor when drawn, so the HUD keeps a consistent physical size on HiDPI
// displays. Set from the monitor content scale (and a user override) BEFORE
// clayInit and on change. clayUiScale() is used by App for minimap hit-testing.
void claySetUiScale(float scale);
float clayUiScale();

// Call once after ImGui has been initialised (GL context + fonts loaded).
void clayInit(int w, int h);

// Call whenever the framebuffer is resized.
void clayResize(int w, int h);

// Returns true if the mouse cursor was over a Clay UI element during the
// most recent clayFrame() call. Use to suppress world hover/click events.
bool clayIsPointerOverUI();

// Returns true if the cursor was over the minimap panel last frame.
// Used by App.cpp to dispatch click-to-walk and scroll zoom for the minimap.
bool clayMinimapHovered();

// Toggle Clay's built-in debug overlay (shows element IDs, bounding boxes,
// layout info). Call before clayFrame() each frame when the toggle is live.
void claySetDebugMode(bool enabled);

// Single-call Clay frame: runs layout, renders output, handles input.
// Call after ImGui::NewFrame() and before ImGui::Render().
// contextVerb / contextSubject: pre-computed top-left context info strings.
// tooltipText: cursor-following tooltip text (nullptr or empty = no tooltip).
// Both may be nullptr or empty.
void clayFrame(const shared::PlayerState* player,
               net::NetworkClient* netc,
               const SpriteCache*  sprites,
               UiHoverState*       hover,
               float dt,
               float mx, float my,
               float screenW, float screenH,
               bool mouseDown,
               bool leftClicked,
               bool rightClicked,
               const char* contextVerb,
               const char* contextSubject,
               float wheelDelta,
               bool showLoginModal,
               bool showJoinModal,
               bool bankOpen,
               unsigned int minimapTex = 0);

} // namespace ui
