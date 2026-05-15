#pragma once

namespace render {

// Installs a KHR_debug callback that prints OpenGL errors / warnings to stderr.
// No-op if the GL context doesn't expose GL_KHR_debug (it does on 4.3+).
void installGlDebugCallback();

}  // namespace render
