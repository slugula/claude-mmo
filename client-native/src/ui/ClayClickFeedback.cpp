// ClayClickFeedback.cpp
// Animated expanding circle at cursor click position, rendered as a Clay
// ATTACH_TO_ROOT floating element with a large corner radius (pill/circle shape).
// 450ms lifetime: alpha fades out, radius expands 60% from base 9px.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayClickFeedback.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

// ── State ─────────────────────────────────────────────────────────────────────
static bool  s_active    = false;
static float s_x         = 0.f;
static float s_y         = 0.f;
static int   s_colorType = 0;   // 0=yellow, 1=red
static float s_elapsed   = 0.f;

static constexpr float kDuration  = 0.45f;
static constexpr float kBaseR     = 9.f;   // base radius in px
static constexpr float kExpandMul = 0.6f;  // grows 60% of base radius over lifetime

// ── API ───────────────────────────────────────────────────────────────────────
void clickFeedbackSpawn(float x, float y, int colorType) {
    s_x         = x;
    s_y         = y;
    s_colorType = colorType;
    s_elapsed   = 0.f;
    s_active    = true;
}

void buildClickFeedback(float dt) {
    if (!s_active) return;

    s_elapsed += dt;
    if (s_elapsed >= kDuration) {
        s_active = false;
        return;
    }

    const float t      = s_elapsed / kDuration;
    const float radius = kBaseR * (1.f + kExpandMul * t);
    const float alpha  = 1.f - t;

    // Fill colour (Clay_Color uses float fields 0-255 range)
    const float a_fill   = alpha * 140.f;
    const float a_border = alpha * 230.f;

    Clay_Color fillColor, borderColor;
    if (s_colorType == 0) {
        // Yellow — walk click
        fillColor   = { 255.f, 220.f,  80.f, a_fill   };
        borderColor = { 255.f, 200.f,  40.f, a_border };
    } else {
        // Red — blocked / action click
        fillColor   = { 255.f,  60.f,  60.f, a_fill   };
        borderColor = { 255.f,  30.f,  30.f, a_border };
    }

    // Center the floating element at click position.
    // Clay floating offset is the top-left corner, so subtract radius.
    float ox = s_x - radius;
    float oy = s_y - radius;
    float sz = radius * 2.f;

    CLAY(CLAY_ID("ClickFeedback"), {
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 40,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("ClickFeedbackCircle"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz) },
            },
            .backgroundColor = fillColor,
            .cornerRadius    = CLAY_CORNER_RADIUS(sz * 0.5f),
            .border = {
                .color = borderColor,
                .width = CLAY_BORDER_ALL(2),
            }
        }) {}
    }
}

} // namespace ui
