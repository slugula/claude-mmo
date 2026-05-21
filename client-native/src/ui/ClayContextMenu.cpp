// ClayContextMenu.cpp
// OSRS-style right-click context menu as a Clay floating panel.
// Visual spec: bg #1a0d00, border #8b6c3e 1px, radius 3, header white 11px,
// separator #3d2010, hover bg #2d1b0e, verb white / subject orange / cancel red.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayContextMenu.hpp"

#include <cstring>
#include <algorithm>

namespace ui {

// ── Singleton ─────────────────────────────────────────────────────────────────
CtxMenuState& ctxMenu() {
    static CtxMenuState s;
    return s;
}

// ── Colours ───────────────────────────────────────────────────────────────────
static constexpr Clay_Color kBg         = {  26,  13,   0, 245 };
static constexpr Clay_Color kBorder     = { 139, 108,  62, 200 };
static constexpr Clay_Color kHeader     = { 255, 255, 255, 255 };
static constexpr Clay_Color kSeparator  = {  61,  32,  16, 255 };
static constexpr Clay_Color kEntryBg    = {   0,   0,   0,   0 };
static constexpr Clay_Color kEntryHover = {  45,  27,  14, 255 };
static constexpr Clay_Color kVerbColor  = { 255, 255, 255, 255 };
static constexpr Clay_Color kSubjColor  = { 255, 152,  31, 255 };
static constexpr Clay_Color kCancelColor= { 255,  68,  68, 255 };

// Temp string scratch (reset each frame in buildContextMenu)
static char  s_buf[4096];
static int   s_off = 0;

static Clay_String cs(const std::string& s) {
    int len = static_cast<int>(s.size());
    if (s_off + len + 1 > (int)sizeof(s_buf)) len = (int)sizeof(s_buf) - s_off - 1;
    if (len <= 0) return { false, 0, "" };
    char* dst = s_buf + s_off;
    std::memcpy(dst, s.c_str(), len);
    dst[len] = '\0';
    s_off += len + 1;
    return { false, len, dst };
}

// ── Build ─────────────────────────────────────────────────────────────────────
void buildContextMenu() {
    CtxMenuState& m = ctxMenu();
    if (!m.open) return;

    s_off = 0;

    // Panel width: wide enough for typical entries
    constexpr float kMenuW    = 160.f;
    constexpr float kEntryH   =  18.f;
    constexpr float kHeaderH  =  22.f;
    constexpr float kSepH     =   1.f;
    constexpr float kPadX     =   8.f;

    // Compute height
    float menuH = kHeaderH + kSepH +
                  static_cast<float>(m.entries.size()) * kEntryH +
                  kEntryH  // "Cancel" entry
                  + 4.f;   // bottom padding

    // Clamp to screen
    float ox = std::min(m.x, m.screenW - kMenuW  - 4.f);
    float oy = std::min(m.y, m.screenH - menuH   - 4.f);
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    CLAY(CLAY_ID("CtxMenuAnchor"), {
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 50,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("CtxMenuPanel"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kMenuW), CLAY_SIZING_GROW(0) },
                .padding         = { 0, 0, 4, 4 },
                .childGap        = 0,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(3),
            .border = {
                .color = kBorder,
                .width = CLAY_BORDER_ALL(1),
            }
        }) {
            // Header
            CLAY(CLAY_ID("CtxHeader"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kHeaderH) },
                    .padding        = { (uint16_t)kPadX, (uint16_t)kPadX, 0, 0 },
                    .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                }
            }) {
                CLAY_TEXT(CLAY_STRING("Choose Option"), CLAY_TEXT_CONFIG({
                    .textColor = kHeader,
                    .fontSize  = 11,
                }));
            }

            // Separator
            CLAY(CLAY_ID("CtxSep"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kSepH) },
                },
                .backgroundColor = kSeparator,
            }) {}

            // Entries
            int entryCount = static_cast<int>(m.entries.size());
            for (int i = 0; i < entryCount; ++i) {
                const auto& e  = m.entries[i];
                bool hovered   = Clay_PointerOver(CLAY_IDI("CtxEntry", i));

                CLAY(CLAY_IDI("CtxEntry", i), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kEntryH) },
                        .padding        = { (uint16_t)kPadX, (uint16_t)kPadX, 0, 0 },
                        .childGap       = 4,
                        .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                        .layoutDirection= CLAY_LEFT_TO_RIGHT,
                    },
                    .backgroundColor = hovered ? kEntryHover : kEntryBg,
                }) {
                    // Verb (white)
                    if (!e.verb.empty()) {
                        CLAY_TEXT(cs(e.verb), CLAY_TEXT_CONFIG({
                            .textColor = kVerbColor,
                            .fontSize  = 11,
                        }));
                    }
                    // Subject (orange)
                    if (!e.subject.empty()) {
                        CLAY_TEXT(cs(e.subject), CLAY_TEXT_CONFIG({
                            .textColor = kSubjColor,
                            .fontSize  = 11,
                        }));
                    }
                }
            }

            // Separator before Cancel
            CLAY(CLAY_ID("CtxSep2"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kSepH) },
                },
                .backgroundColor = kSeparator,
            }) {}

            // Cancel entry
            {
                bool hovered = Clay_PointerOver(CLAY_ID("CtxCancel"));
                CLAY(CLAY_ID("CtxCancel"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kEntryH) },
                        .padding        = { (uint16_t)kPadX, (uint16_t)kPadX, 0, 0 },
                        .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = hovered ? kEntryHover : kEntryBg,
                }) {
                    CLAY_TEXT(CLAY_STRING("Cancel"), CLAY_TEXT_CONFIG({
                        .textColor = kCancelColor,
                        .fontSize  = 11,
                    }));
                }
            }
        }
    }
}

// ── Input handling ─────────────────────────────────────────────────────────────
bool handleContextMenuInput(bool leftClicked, float mx, float my) {
    (void)mx; (void)my;
    CtxMenuState& m = ctxMenu();
    if (!m.open) return false;

    m.clickedIndex = -1;

    if (!leftClicked) return false;

    // Check each entry
    int n = static_cast<int>(m.entries.size());
    for (int i = 0; i < n; ++i) {
        if (Clay_PointerOver(CLAY_IDI("CtxEntry", i))) {
            m.clickedIndex = i;
            m.open = false;
            return true;
        }
    }

    // Cancel entry or anywhere outside
    m.open = false;
    return true;
}

} // namespace ui
