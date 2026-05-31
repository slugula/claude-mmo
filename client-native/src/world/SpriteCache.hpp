#pragma once
// SpriteCache — loads a GL texture for every item's sprite PNG (DB-driven via
// item_definitions.sprite_path). No procedural/placeholder art: the DB is the
// single source of truth. Items with no sprite (or a failed load) fall back to
// a neutral square so slots aren't invisible.
//
// Ownership: App creates one SpriteCache, calls load() after the GL context is
// ready AND item definitions have been fetched, passes a const pointer to
// clayFrame() so ClayHudPanel can call get().

#include <glad/glad.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

class SpriteCache {
public:
    // One item's resolved sprite: its id and an absolute path to a PNG on disk.
    struct Entry {
        std::string id;
        std::string absPath;
    };

    // (Re)build the cache from the given item sprites. Loads each PNG via
    // stb_image. Must be called with a valid GL context.
    void load(const std::vector<Entry>& entries);

    // GL texture for itemId, or the neutral fallback if the id has no sprite.
    GLuint get(const std::string& itemId) const;

    // Deletes all GL textures. Call before GL context destruction.
    void destroy();

private:
    std::unordered_map<std::string, GLuint> cache_;
    GLuint                                  fallback_ = 0;  // neutral "no sprite" square
};

} // namespace ui
