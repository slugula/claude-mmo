#include "ui/UiAudio.hpp"

namespace ui {
namespace {
std::function<void(const char*)> g_sfxHook;
}

void setSfxHook(std::function<void(const char*)> hook) { g_sfxHook = std::move(hook); }

void sfx(const char* name) { if (g_sfxHook && name) g_sfxHook(name); }

}  // namespace ui
