// ClayContextInfo.cpp
// Fixed ATTACH_TO_ROOT floating element at (12, 12) showing:
//   verb (white 15px) + subject (orange 15px) — double-rendered for text shadow.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayContextInfo.hpp"

#include <cstring>

namespace ui {

static constexpr Clay_Color kWhite  = { 255.f, 255.f, 255.f, 255.f };
static constexpr Clay_Color kOrange = { 255.f, 152.f,  31.f, 255.f };
static constexpr Clay_Color kShadow = {   0.f,   0.f,   0.f, 160.f };
static constexpr Clay_Color kTransp = {   0.f,   0.f,   0.f,   0.f };

// Build a Clay_String from a non-literal const char*.
static Clay_String cstr(const char* s) {
    return { false, static_cast<int>(std::strlen(s)), s };
}

void buildContextInfo(const char* verb, const char* subject) {
    bool hasVerb = verb    && verb[0]    != '\0';
    bool hasSubj = subject && subject[0] != '\0';
    if (!hasVerb && !hasSubj) return;

    // ── Shadow pass (offset +1,+1) ────────────────────────────────────────────
    CLAY(CLAY_ID("CtxInfoShadowAnchor"), {
        .floating = {
            .offset       = { 13.f, 13.f },
            .zIndex       = 20,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("CtxInfoShadowRow"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                .childGap        = 4,
                .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
            .backgroundColor = kTransp,
        }) {
            if (hasVerb) {
                CLAY_TEXT(cstr(verb), CLAY_TEXT_CONFIG({
                    .textColor = kShadow,
                    .fontSize  = 15,
                }));
            }
            if (hasSubj) {
                CLAY_TEXT(cstr(subject), CLAY_TEXT_CONFIG({
                    .textColor = kShadow,
                    .fontSize  = 15,
                }));
            }
        }
    }

    // ── Colour pass ───────────────────────────────────────────────────────────
    CLAY(CLAY_ID("CtxInfoColorAnchor"), {
        .floating = {
            .offset       = { 12.f, 12.f },
            .zIndex       = 21,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("CtxInfoColorRow"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                .childGap        = 4,
                .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
            .backgroundColor = kTransp,
        }) {
            if (hasVerb) {
                CLAY_TEXT(cstr(verb), CLAY_TEXT_CONFIG({
                    .textColor = kWhite,
                    .fontSize  = 15,
                }));
            }
            if (hasSubj) {
                CLAY_TEXT(cstr(subject), CLAY_TEXT_CONFIG({
                    .textColor = kOrange,
                    .fontSize  = 15,
                }));
            }
        }
    }
}

} // namespace ui
