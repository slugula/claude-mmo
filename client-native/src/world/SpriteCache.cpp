// SpriteCache.cpp
// DB-driven item sprites: each item's sprite_path PNG is loaded into a GL
// texture at startup. No procedural art — the DB is the single source of truth.

#include "world/SpriteCache.hpp"

#include <stb_image.h>

#include <cstdint>
#include <cstdio>

namespace ui {

namespace {

GLuint uploadRGBA(const unsigned char* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // Nearest, no mipmaps — sprites are authored at slot resolution (32×32) and
    // drawn 1:1, so any filtering would only blur them. Keep them crisp.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// A neutral 2×2 grey square — shown for items that have no sprite assigned in
// the DB (so an item slot is never invisible). Not per-item placeholder art.
GLuint makeFallback() {
    const unsigned char px[2 * 2 * 4] = {
        90, 80, 60, 160,   90, 80, 60, 160,
        90, 80, 60, 160,   90, 80, 60, 160,
    };
    return uploadRGBA(px, 2, 2);
}

} // namespace

void SpriteCache::load(const std::vector<Entry>& entries) {
    destroy();
    fallback_ = makeFallback();

    // PNGs are authored top-down; ImGui DrawList::AddImage samples uv(0,0) at the
    // top-left, so do NOT flip vertically.
    stbi_set_flip_vertically_on_load(false);

    for (const auto& e : entries) {
        if (e.id.empty() || e.absPath.empty()) continue;
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(e.absPath.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::fprintf(stderr, "[SpriteCache] failed to load sprite '%s' (%s): %s\n",
                         e.id.c_str(), e.absPath.c_str(), stbi_failure_reason());
            continue;
        }
        cache_[e.id] = uploadRGBA(data, w, h);
        stbi_image_free(data);
    }
    std::fprintf(stdout, "[SpriteCache] loaded %zu item sprites\n", cache_.size());
}

GLuint SpriteCache::get(const std::string& itemId) const {
    auto it = cache_.find(itemId);
    return (it != cache_.end()) ? it->second : fallback_;
}

void SpriteCache::destroy() {
    for (auto& [id, tex] : cache_) glDeleteTextures(1, &tex);
    cache_.clear();
    if (fallback_) { glDeleteTextures(1, &fallback_); fallback_ = 0; }
}

} // namespace ui
