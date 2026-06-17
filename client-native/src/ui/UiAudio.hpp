#pragma once

#include <functional>

namespace ui {

// Lightweight SFX bridge so UI modules (Clay panels) can trigger named sounds
// without depending on the AudioEngine directly. App installs the hook at init
// (forwarding to AudioEngine::playSfx); UI code calls ui::sfx("name").
void setSfxHook(std::function<void(const char*)> hook);
void sfx(const char* name);

}  // namespace ui
