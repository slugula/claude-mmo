#pragma once

// Clay types are fully hidden behind this header.
// App.cpp never needs to include clay.h.

#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"    // UiHoverState

namespace net { class NetworkClient; }

namespace ui {

// Call once after ImGui has been initialised (GL context + fonts loaded).
void clayInit(int w, int h);

// Call whenever the framebuffer is resized.
void clayResize(int w, int h);

// Single-call Clay frame: runs layout, renders output, handles input.
// Call after ImGui::NewFrame() and before ImGui::Render().
void clayFrame(const shared::PlayerState* player,
               net::NetworkClient* netc,
               UiHoverState*       hover,
               float dt,
               float mx, float my,
               bool mouseDown,
               bool leftClicked,
               bool rightClicked);

} // namespace ui
