#pragma once
// SpriteCache — pre-renders a 32×32 GL_RGBA8 texture for every known item ID
// at startup.  The textures are used by ClayHudPanel to display item sprites
// inside Clay IMAGE commands.
//
// Ownership: App creates one SpriteCache, calls init() after the GL context is
// ready, passes a const pointer to clayFrame() so ClayHudPanel can call get().

#include <glad/glad.h>
#include <string>
#include <unordered_map>

namespace ui {

class SpriteCache {
public:
    // Must be called with a valid GL context (after initImGui / after GLAD init).
    void init();

    // Returns the GL texture name for itemId, or the solid-colour fallback if
    // the id is unknown.  Always valid after init().
    GLuint get(const std::string& itemId) const;

    // Deletes all GL textures.  Call before GL context destruction.
    void destroy();

private:
    // Upload a 32×32 RGBA pixel buffer as a new GL texture and return its name.
    static GLuint upload(const uint32_t* rgba32);

    // Build every item texture and populate cache_.
    void buildAll();

    std::unordered_map<std::string, GLuint> cache_;
    GLuint                                  fallback_ = 0;  // grey square
};

} // namespace ui
