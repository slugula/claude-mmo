// SpriteCache.cpp
// CPU-side pixel-art sprite generation for every item in the game.
// Each sprite is a 32×32 RGBA8 texture uploaded once at startup.
//
// Pixel helpers use RGBA byte order: memory layout is R,G,B,A as expected by
// glTexImage2D with GL_RGBA / GL_UNSIGNED_BYTE.

#include "world/SpriteCache.hpp"

#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ui {

// ── Pixel helpers ─────────────────────────────────────────────────────────────
// Pack RGBA into a uint32_t in the layout GL_RGBA/GL_UNSIGNED_BYTE expects:
//   byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A  (little-endian: stored as ABGR in 32-bit int)
static constexpr uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r)
         | static_cast<uint32_t>(g) << 8
         | static_cast<uint32_t>(b) << 16
         | static_cast<uint32_t>(a) << 24;
}
static constexpr uint32_t kTransparent = RGBA(0, 0, 0, 0);

static constexpr int kW = 32, kH = 32;

struct Buf { uint32_t px[kW * kH]; };

static void fill(Buf& b, uint32_t c) {
    for (auto& p : b.px) p = c;
}
static void setPixel(Buf& b, int x, int y, uint32_t c) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    b.px[y * kW + x] = c;
}
static void rect(Buf& b, int x, int y, int w, int h, uint32_t c) {
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            setPixel(b, x + dx, y + dy, c);
}
static void border(Buf& b, int x, int y, int w, int h, int t, uint32_t c) {
    rect(b, x,         y,         w,  t, c);   // top
    rect(b, x,         y + h - t, w,  t, c);   // bottom
    rect(b, x,         y,         t,  h, c);   // left
    rect(b, x + w - t, y,         t,  h, c);   // right
}
static void circle(Buf& b, int cx, int cy, int r, uint32_t c) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx*dx + dy*dy <= r*r)
                setPixel(b, cx + dx, cy + dy, c);
}
static void line(Buf& b, int x0, int y0, int x1, int y1, int t, uint32_t c) {
    // Simple axis-aligned thick lines for pixel art
    if (std::abs(x1-x0) >= std::abs(y1-y0)) {
        if (x0 > x1) { std::swap(x0,x1); std::swap(y0,y1); }
        for (int x = x0; x <= x1; ++x) {
            int y = y0 + (y1-y0) * (x-x0) / std::max(1, x1-x0);
            rect(b, x, y - t/2, 1, t, c);
        }
    } else {
        if (y0 > y1) { std::swap(x0,x1); std::swap(y0,y1); }
        for (int y = y0; y <= y1; ++y) {
            int x = x0 + (x1-x0) * (y-y0) / std::max(1, y1-y0);
            rect(b, x - t/2, y, t, 1, c);
        }
    }
}

// ── Upload ────────────────────────────────────────────────────────────────────
GLuint SpriteCache::upload(const uint32_t* rgba32) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba32);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ── Sprite builders ───────────────────────────────────────────────────────────

// Axe: gray-silver blade + brown handle
static Buf makeAxe(bool iron = false) {
    Buf b; fill(b, kTransparent);
    uint32_t blade  = iron ? RGBA(160,170,180) : RGBA(180,185,190);
    uint32_t edge   = iron ? RGBA(220,230,240) : RGBA(210,215,220);
    uint32_t handle = RGBA(120, 80, 40);
    uint32_t grip   = RGBA( 90, 55, 25);

    // Handle (vertical, right side)
    rect(b, 18, 10, 4, 18, handle);
    rect(b, 19, 10, 2, 18, grip);

    // Blade (left side, upper half — axe head shape)
    //  Wide fan shape
    rect(b,  7, 8,  12, 3, blade);  // top of blade
    rect(b,  9, 11, 10, 3, blade);
    rect(b, 11, 14,  8, 3, blade);
    rect(b, 13, 17,  6, 2, blade);

    // Bright edge on blade left
    rect(b,  7, 8,   2, 11, edge);

    // Brown binding where blade meets handle
    rect(b, 17, 13,  2,  5, RGBA(90, 60, 20));

    return b;
}

// Pickaxe: dark gray pick head + brown handle
static Buf makePickaxe() {
    Buf b; fill(b, kTransparent);
    uint32_t metal  = RGBA(130,130,140);
    uint32_t bright = RGBA(200,200,210);
    uint32_t handle = RGBA(120, 80, 40);
    uint32_t grip   = RGBA( 90, 55, 25);

    // Handle (diagonal, bottom-right area)
    for (int i = 0; i < 16; ++i)
        rect(b, 16 + i/2, 16 + i, 3, 1, handle);
    for (int i = 0; i < 16; ++i)
        setPixel(b, 17 + i/2, 16 + i, grip);

    // Pick head (horizontal left-to-right, upper area)
    rect(b,  4, 10, 22,  5, metal);
    rect(b,  4, 10,  3,  5, bright);   // left tip bright
    rect(b, 23, 10,  3,  5, bright);   // right tip bright
    rect(b,  4, 10, 22,  1, bright);   // top edge bright

    // Collar
    rect(b, 13, 14,  6,  3, RGBA(80,60,30));

    return b;
}

// Sword: bright silver blade + golden crossguard + brown grip
static Buf makeSword(bool longsword = false) {
    Buf b; fill(b, kTransparent);
    uint32_t blade  = RGBA(190,195,205);
    uint32_t edge   = RGBA(230,235,245);
    uint32_t guard  = RGBA(190,150, 30);
    uint32_t handle = RGBA(110, 70, 30);
    uint32_t pommel = RGBA(160,130, 40);

    int bladeH = longsword ? 20 : 16;

    // Blade
    rect(b, 14,  4,  4, bladeH, blade);
    rect(b, 15,  4,  2, bladeH, edge);
    // Tip
    setPixel(b, 14, 4 + bladeH,     blade);
    setPixel(b, 15, 4 + bladeH,     edge);
    setPixel(b, 16, 4 + bladeH,     blade);
    setPixel(b, 15, 4 + bladeH + 1, edge);

    // Crossguard
    rect(b,  8, 4 + bladeH + 1,  16, 3, guard);
    rect(b,  8, 4 + bladeH + 1,  16, 1, RGBA(210,175, 50));

    // Handle
    rect(b, 14, 4 + bladeH + 4,  4, 7, handle);
    rect(b, 15, 4 + bladeH + 4,  2, 7, RGBA(130, 90, 40));

    // Pommel
    circle(b, 15, 4 + bladeH + 11, 3, pommel);

    return b;
}

// Shield: rounded square, dark metal
static Buf makeShield() {
    Buf b; fill(b, kTransparent);
    uint32_t metal  = RGBA( 80, 80, 90);
    uint32_t bright = RGBA(140,140,150);
    uint32_t boss   = RGBA(160,130, 40);

    rect(b,  6,  4, 20, 24, metal);
    // Rounded corners (cut out)
    for (int i = 0; i < 3; ++i) {
        setPixel(b,  6, 4 + i,   kTransparent);
        setPixel(b,  7, 4 + i,   kTransparent);
        setPixel(b, 25, 4 + i,   kTransparent);
        setPixel(b, 24, 4 + i,   kTransparent);
        setPixel(b,  6, 27 - i,  kTransparent);
        setPixel(b,  7, 27 - i,  kTransparent);
        setPixel(b, 25, 27 - i,  kTransparent);
        setPixel(b, 24, 27 - i,  kTransparent);
    }
    // Bright left edge
    rect(b,  6,  7,  2, 18, bright);
    // Boss (center stud)
    circle(b, 16, 15, 4, boss);
    setPixel(b, 15, 14, RGBA(200,175, 60));

    return b;
}

// Logs: brown rectangle with grain lines
static Buf makeLogs(int variant = 0) {
    // variant: 0=logs, 1=oak_logs, 2=willow_logs
    Buf b; fill(b, kTransparent);
    uint32_t wood   = variant == 0 ? RGBA(140, 90, 40)
                    : variant == 1 ? RGBA(160,110, 50)
                    :                RGBA(110,140, 70);
    uint32_t light  = variant == 0 ? RGBA(180,120, 60)
                    : variant == 1 ? RGBA(200,140, 70)
                    :                RGBA(140,170, 90);
    uint32_t dark   = variant == 0 ? RGBA(100, 65, 25)
                    : variant == 1 ? RGBA(120, 80, 35)
                    :                RGBA( 80,110, 50);

    rect(b,  4, 10, 24, 12, wood);
    // Grain lines
    for (int x = 4; x < 28; x += 4) {
        rect(b, x, 10, 1, 12, dark);
    }
    rect(b, 4, 10, 24, 1, light);   // top highlight
    // End grain circles
    circle(b, 28, 16, 5, light);
    circle(b, 28, 16, 2, wood);
    circle(b,  4, 16, 5, light);
    circle(b,  4, 16, 2, wood);

    return b;
}

// Chaingun: gray barrel + body, orange highlights
static Buf makeChaingun() {
    Buf b; fill(b, kTransparent);
    uint32_t body   = RGBA( 90, 90,100);
    uint32_t barrel = RGBA(130,130,140);
    uint32_t bright = RGBA(170,170,180);
    uint32_t accent = RGBA(200,100, 20);

    // Stock
    rect(b,  4, 18, 12,  8, RGBA(80,60,40));
    rect(b,  4, 18, 12,  2, RGBA(100,80,50));

    // Main body
    rect(b,  8, 12, 16, 10, body);
    rect(b,  8, 12, 16,  2, bright);

    // Barrel (extends right)
    rect(b, 22, 13, 8,  5, barrel);
    rect(b, 22, 13, 8,  1, bright);
    rect(b, 28, 14, 4,  3, barrel);

    // Trigger guard
    rect(b, 13, 22,  6,  2, RGBA(70,70,80));

    // Orange strip accent
    rect(b,  9, 14, 14,  2, accent);

    // Scope / grip detail
    rect(b, 10, 10,  4,  3, RGBA(60,60,70));

    return b;
}

// Helmet: crude dome
static Buf makeHelm(bool bronze = false) {
    Buf b; fill(b, kTransparent);
    uint32_t metal  = bronze ? RGBA(180,100, 30) : RGBA(110,120, 80);
    uint32_t bright = bronze ? RGBA(210,130, 50) : RGBA(150,160,110);
    uint32_t dark   = bronze ? RGBA(130, 70, 10) : RGBA( 70, 80, 50);

    // Dome
    circle(b, 16, 14, 10, metal);
    // Highlight
    rect(b, 10,  6, 12,  3, bright);
    // Cheek guards
    rect(b,  6, 18,  5,  8, metal);
    rect(b, 21, 18,  5,  8, metal);
    // Visor slot
    rect(b, 10, 18, 12,  3, dark);
    rect(b, 10, 18, 12,  1, RGBA(30,30,30));

    return b;
}

// Leather body armour: brown chest
static Buf makeBody() {
    Buf b; fill(b, kTransparent);
    uint32_t leather = RGBA(120, 80, 40);
    uint32_t dark    = RGBA( 80, 50, 20);
    uint32_t seam    = RGBA( 90, 60, 25);
    uint32_t bright  = RGBA(155,105, 55);

    // Main body
    rect(b,  6,  4, 20, 22, leather);
    // Collar cutout
    rect(b, 10,  4, 12,  5, kTransparent);
    rect(b,  6,  4,  4,  3, kTransparent);
    rect(b, 22,  4,  4,  3, kTransparent);
    // Shoulder pads
    rect(b,  4,  7,  6,  6, leather);
    rect(b, 22,  7,  6,  6, leather);
    // Arm holes
    rect(b,  4, 13,  3, 10, kTransparent);
    rect(b, 25, 13,  3, 10, kTransparent);
    // Seams
    rect(b, 14,  7,  2, 19, seam);
    rect(b,  6, 12, 20,  1, dark);
    // Bottom trim
    rect(b,  6, 25, 20,  1, bright);

    return b;
}

// Leather legs: brown pants
static Buf makeLegs() {
    Buf b; fill(b, kTransparent);
    uint32_t leather = RGBA(120, 80, 40);
    uint32_t dark    = RGBA( 80, 50, 20);
    uint32_t seam    = RGBA( 90, 60, 25);

    // Waistband
    rect(b,  6,  3, 20,  4, dark);
    // Left leg
    rect(b,  6,  7, 10, 22, leather);
    // Right leg
    rect(b, 16,  7, 10, 22, leather);
    // Gap between legs
    rect(b, 14, 10,  4, 18, kTransparent);
    // Seam / lacing
    rect(b, 14,  3,  4,  7, RGBA(100,65,30));
    // Bottom hems
    rect(b,  6, 28, 10,  2, dark);
    rect(b, 16, 28, 10,  2, dark);
    // Side trim
    rect(b,  6,  7,  1, 20, seam);
    rect(b, 25,  7,  1, 20, seam);
    (void)seam;

    return b;
}

// Boots / gloves: small brown
static Buf makeBoots() {
    Buf b; fill(b, kTransparent);
    uint32_t leather = RGBA(100, 65, 30);
    uint32_t dark    = RGBA( 70, 40, 15);
    uint32_t sole    = RGBA( 50, 30, 10);

    // Left boot
    rect(b,  4, 10,  9, 14, leather);
    rect(b,  4, 22,  9,  2, dark);
    rect(b,  3, 24, 10,  2, sole);
    // Toe extension
    rect(b,  3, 22,  2,  4, dark);

    // Right boot
    rect(b, 19, 10,  9, 14, leather);
    rect(b, 19, 22,  9,  2, dark);
    rect(b, 19, 24, 10,  2, sole);
    rect(b, 27, 22,  2,  4, dark);

    return b;
}

static Buf makeGloves() {
    Buf b; fill(b, kTransparent);
    uint32_t leather = RGBA(120, 80, 40);
    uint32_t dark    = RGBA( 80, 50, 20);

    // Left glove
    rect(b,  4,  8,  9, 12, leather);
    // Fingers
    for (int i = 0; i < 4; ++i)
        rect(b,  4 + i*2,  4, 2,  5, leather);
    rect(b,  4, 19,  9,  2, dark);

    // Right glove
    rect(b, 19,  8,  9, 12, leather);
    for (int i = 0; i < 4; ++i)
        rect(b, 19 + i*2,  4, 2,  5, leather);
    rect(b, 19, 19,  9,  2, dark);

    return b;
}

// Amulet: gold teardrop on string
static Buf makeAmulet() {
    Buf b; fill(b, kTransparent);
    uint32_t gold   = RGBA(200,160, 20);
    uint32_t bright = RGBA(240,210, 60);
    uint32_t string = RGBA(140,100, 60);
    uint32_t gem    = RGBA( 30,180,220);

    // String
    line(b, 16, 2, 16, 12, 1, string);
    line(b,  9, 3, 16,  2, 1, string);
    line(b, 23, 3, 16,  2, 1, string);

    // Pendant
    circle(b, 16, 18, 8, gold);
    circle(b, 15, 17, 8, gold);  // teardrop offset
    rect(b, 14,  9,  4,  4, gold);  // upper part
    // Bright top
    circle(b, 14, 14, 3, bright);
    // Gem in center
    circle(b, 15, 18, 3, gem);

    return b;
}

// Coins: gold stack
static Buf makeCoins() {
    Buf b; fill(b, kTransparent);
    uint32_t gold   = RGBA(200,165, 20);
    uint32_t bright = RGBA(240,210, 60);
    uint32_t dark   = RGBA(140,110, 10);

    // Stack of coins (multiple ellipses)
    // Bottom coin
    rect(b,  8, 22, 16,  5, gold);
    rect(b,  8, 22, 16,  1, bright);
    rect(b,  8, 26, 16,  1, dark);
    // Mid coins
    rect(b,  9, 18, 14,  5, gold);
    rect(b,  9, 18, 14,  1, bright);
    rect(b,  9, 22, 14,  1, dark);
    // Top coin
    rect(b, 10, 14, 12,  5, gold);
    rect(b, 10, 14, 12,  1, bright);
    rect(b, 10, 18, 12,  1, dark);
    // Coin faces (slight ellipse tops)
    circle(b, 16, 14, 5, gold);
    rect(b, 11, 11,  10, 4, gold);
    circle(b, 16, 11, 4, bright);

    return b;
}

// Generic ore rock: gray/brown lumpy circle
static Buf makeOre(uint32_t color) {
    Buf b; fill(b, kTransparent);
    uint32_t dark   = RGBA(30,30,30);
    uint32_t bright = RGBA(
        static_cast<uint8_t>(std::min(255, static_cast<int>((color & 0xFF) * 4 / 3))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(((color >> 8) & 0xFF) * 4 / 3))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(((color >> 16) & 0xFF) * 4 / 3)))
    );

    circle(b, 15, 17, 11, color);
    circle(b, 18, 14,  8, color);
    circle(b, 13, 19,  7, color);
    // Highlight
    circle(b, 13, 12,  4, bright);
    // Shadow
    circle(b, 18, 21,  5, dark);

    return b;
}

// Bar (smithed): rectangular metallic bar
static Buf makeBar(uint32_t color) {
    Buf b; fill(b, kTransparent);
    uint32_t bright = RGBA(
        static_cast<uint8_t>(std::min(255, static_cast<int>((color & 0xFF) + 60))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(((color >> 8) & 0xFF) + 40))),
        static_cast<uint8_t>(std::min(255, static_cast<int>(((color >> 16) & 0xFF) + 20)))
    );
    uint32_t dark = RGBA(
        static_cast<uint8_t>(std::max(0, static_cast<int>((color & 0xFF) - 40))),
        static_cast<uint8_t>(std::max(0, static_cast<int>(((color >> 8) & 0xFF) - 30))),
        static_cast<uint8_t>(std::max(0, static_cast<int>(((color >> 16) & 0xFF) - 20)))
    );

    rect(b,  5, 10, 22, 12, color);
    rect(b,  5, 10, 22,  2, bright);   // top face
    rect(b,  5, 20, 22,  2, dark);     // bottom
    rect(b, 25, 10,  2, 12, RGBA(
        static_cast<uint8_t>((color & 0xFF) / 2),
        static_cast<uint8_t>(((color >> 8) & 0xFF) / 2),
        static_cast<uint8_t>(((color >> 16) & 0xFF) / 2)
    ));  // right edge (dark side)
    rect(b, 5, 10, 2, 12, bright);     // left edge bright

    return b;
}

// Arrow: thin shaft with feather + tip
static Buf makeArrow() {
    Buf b; fill(b, kTransparent);
    uint32_t shaft  = RGBA(160,130, 80);
    uint32_t tip    = RGBA(170,175,185);
    uint32_t feather= RGBA(230,230,230);
    uint32_t feath2 = RGBA(200, 80, 80);

    // Shaft (diagonal for visual interest)
    for (int i = 0; i < 22; ++i) {
        int x = 8 + i;
        int y = 24 - i;
        setPixel(b, x, y, shaft);
        setPixel(b, x, y-1, shaft);
    }
    // Tip (top-right)
    rect(b, 25,  5,  4,  2, tip);
    rect(b, 27,  4,  2,  3, tip);
    setPixel(b, 29, 3, tip);
    // Feathers (bottom-left)
    rect(b,  4, 24,  6,  2, feather);
    rect(b,  4, 23,  4,  2, feath2);
    rect(b,  5, 26,  5,  2, feath2);

    return b;
}

// Food (shrimp/trout): orange-pink curve
static Buf makeFood(bool fish = false) {
    Buf b; fill(b, kTransparent);
    uint32_t col    = fish ? RGBA(210,120, 60) : RGBA(240,160, 80);
    uint32_t bright = fish ? RGBA(240,160, 90) : RGBA(255,200,120);
    uint32_t dark   = fish ? RGBA(160, 80, 30) : RGBA(190,110, 40);

    if (!fish) {
        // Shrimp: curved C shape
        for (int i = 0; i < 12; ++i) {
            float angle = (float)i / 11.0f * 3.14159f * 1.2f;
            int x = 16 + (int)(10.f * std::cos(angle));
            int y = 16 - (int)( 9.f * std::sin(angle));
            rect(b, x - 1, y - 1, 4, 4, col);
        }
        // Highlight
        for (int i = 2; i < 8; ++i) {
            float angle = (float)i / 11.0f * 3.14159f * 1.2f;
            int x = 16 + (int)(10.f * std::cos(angle));
            int y = 16 - (int)( 9.f * std::sin(angle));
            setPixel(b, x, y, bright);
        }
        // Antennae
        line(b, 22, 9, 28, 3, 1, dark);
        line(b, 23, 8, 30, 7, 1, dark);
    } else {
        // Trout: fish outline
        // Body ellipse
        for (int dy = -7; dy <= 7; ++dy)
            for (int dx = -11; dx <= 11; ++dx)
                if ((dx*dx*49 + dy*dy*121) <= 49*121)
                    setPixel(b, 15 + dx, 16 + dy, col);
        // Bright belly
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -7; dx <= 7; ++dx)
                if ((dx*dx*9 + dy*dy*49) <= 9*49)
                    setPixel(b, 14 + dx, 18 + dy, bright);
        // Eye
        circle(b, 22, 13, 2, RGBA(20,20,20));
        setPixel(b, 22, 13, bright);
        // Tail
        rect(b,  3, 12, 5, 8, col);
        rect(b,  2, 11, 4, 10, dark);
    }
    return b;
}

// Ring: gold band
static Buf makeRing() {
    Buf b; fill(b, kTransparent);
    uint32_t gold   = RGBA(200,160, 20);
    uint32_t bright = RGBA(240,210, 60);
    uint32_t gem    = RGBA(220, 30, 30);

    // Ring band (thick circle outline)
    for (int dy = -10; dy <= 10; ++dy)
        for (int dx = -10; dx <= 10; ++dx) {
            int d2 = dx*dx + dy*dy;
            if (d2 <= 100 && d2 >= 64)
                setPixel(b, 16 + dx, 16 + dy, gold);
        }
    // Highlight
    for (int i = 0; i < 6; ++i)
        setPixel(b, 10 + i, 10, bright);
    // Setting + gem at top
    rect(b, 13,  6,  6,  5, gold);
    circle(b, 16,  8, 3, gem);
    setPixel(b, 15, 7, RGBA(255, 80, 80));

    return b;
}

// Kinetic charges: glowing energy sphere
static Buf makeKineticCharges() {
    Buf b; fill(b, kTransparent);
    circle(b, 16, 16, 12, RGBA( 20, 60,180, 200));
    circle(b, 16, 16,  9, RGBA( 60,120,230, 220));
    circle(b, 16, 16,  6, RGBA(120,180,255, 230));
    circle(b, 14, 14,  4, RGBA(200,230,255, 240));
    // Energy crackle lines
    line(b, 16,  4, 16, 28, 1, RGBA(180,220,255, 80));
    line(b,  4, 16, 28, 16, 1, RGBA(180,220,255, 80));
    line(b,  8,  8, 24, 24, 1, RGBA(180,220,255, 60));
    return b;
}

// Egg: white/cream oval
static Buf makeEgg() {
    Buf b; fill(b, kTransparent);
    uint32_t shell  = RGBA(240,235,210);
    uint32_t bright = RGBA(255,255,240);
    uint32_t shadow = RGBA(200,190,170);

    // Egg shape: taller circle, squished on one side
    for (int dy = -12; dy <= 10; ++dy)
        for (int dx = -9; dx <= 9; ++dx) {
            float rx = 9.f, ry = dy < 0 ? 12.f : 10.f;
            if ((dx*dx)/(rx*rx) + (dy*dy)/(ry*ry) <= 1.f)
                setPixel(b, 16 + dx, 16 + dy, shell);
        }
    // Highlight
    circle(b, 13, 10, 3, bright);
    setPixel(b, 12, 9, bright);
    // Shadow
    for (int dy = 4; dy <= 9; ++dy)
        for (int dx = -6; dx <= 6; ++dx)
            if (dx*dx*81 + dy*dy*36 <= 81*36)
                setPixel(b, 17 + dx, 16 + dy, shadow);

    return b;
}

// Solid-color fallback square with a white inner border
static Buf makeSolid(uint32_t color) {
    Buf b; fill(b, kTransparent);
    rect(b, 2, 2, 28, 28, color);
    border(b, 2, 2, 28, 28, 1, RGBA(255,255,255,60));
    return b;
}

// ── Build all ─────────────────────────────────────────────────────────────────
void SpriteCache::buildAll() {
    auto put = [&](const char* id, Buf&& buf) {
        cache_[id] = upload(buf.px);
    };

    // Weapons
    put("axe",              makeAxe(false));
    put("iron_axe",         makeAxe(true));
    put("pickaxe",          makePickaxe());
    put("bronze_sword",     makeSword(false));
    put("iron_sword",       makeSword(false));
    put("bronze_longsword", makeSword(true));
    put("basic_chaingun",   makeChaingun());

    // Armour
    put("bronze_shield",    makeShield());
    put("leather_helm",     makeHelm(false));
    put("bronze_helm",      makeHelm(true));
    put("leather_body",     makeBody());
    put("leather_legs",     makeLegs());
    put("leather_gloves",   makeGloves());
    put("leather_boots",    makeBoots());
    put("amulet",           makeAmulet());
    put("gold_ring",        makeRing());

    // Resources
    put("logs",             makeLogs(0));
    put("oak_logs",         makeLogs(1));
    put("willow_logs",      makeLogs(2));
    put("egg",              makeEgg());

    // Currency
    put("coins",            makeCoins());

    // Ammo
    put("arrow",            makeArrow());
    put("kinetic_charges",  makeKineticCharges());

    // Food
    put("shrimp",           makeFood(false));
    put("trout",            makeFood(true));

    // Ores / bars (fallback colors but with ore/bar shape)
    put("copper_ore",    makeOre(RGBA(180,100, 50)));
    put("tin_ore",       makeOre(RGBA(160,160,170)));
    put("iron_ore",      makeOre(RGBA(120, 80, 60)));
    put("coal",          makeOre(RGBA( 40, 40, 45)));
    put("mithril_ore",   makeOre(RGBA( 60, 80,180)));
    put("bronze_bar",    makeBar(RGBA(180,100, 30)));
    put("iron_bar",      makeBar(RGBA(100, 90, 85)));
    put("steel_bar",     makeBar(RGBA(120,120,130)));

    // Fallback
    {
        Buf fb = makeSolid(RGBA(80,60,40));
        fallback_ = upload(fb.px);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void SpriteCache::init() {
    buildAll();
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
