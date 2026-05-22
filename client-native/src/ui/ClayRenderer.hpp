#pragma once

// Clay types are fully hidden behind this header.
// App.cpp never needs to include clay.h.

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"       // UiHoverState
#include "world/SpriteCache.hpp"

namespace net { class NetworkClient; }

namespace ui {

// Call once after ImGui has been initialised (GL context + fonts loaded).
void clayInit(int w, int h);

// Call whenever the framebuffer is resized.
void clayResize(int w, int h);

// Returns true if the mouse cursor was over a Clay UI element during the
// most recent clayFrame() call. Use to suppress world hover/click events.
bool clayIsPointerOverUI();

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
               bool bankOpen);

} // namespace ui
