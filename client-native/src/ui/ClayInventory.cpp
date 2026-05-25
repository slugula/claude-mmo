// ClayInventory.cpp
// Inventory panel laid out with Clay.  All sizing, padding, gaps, and
// flexbox-style child alignment are declared here; the renderer in
// ClayRenderer.cpp translates the output to ImGui DrawList calls.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayInventory.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace ui {

// ── Palette ───────────────────────────────────────────────────────────────────
static constexpr Clay_Color kPanelBg     = {  28,  18,   8, 235 };
static constexpr Clay_Color kPanelBorder = {  90,  60,  20, 255 };
static constexpr Clay_Color kTitleColor  = { 255, 195,  50, 255 };
static constexpr Clay_Color kSlotEmpty   = {  40,  30,  15, 255 };
static constexpr Clay_Color kSlotFilled  = {  55,  42,  20, 255 };
static constexpr Clay_Color kSlotBorder  = {  70,  50,  20, 180 };
static constexpr Clay_Color kQtyColor    = { 255, 255, 100, 255 };

static Clay_Color itemAccent(const std::string& id)
{
    if (id.find("sword")     != std::string::npos ||
        id.find("longsword") != std::string::npos) return { 180, 190, 200, 255 };
    if (id.find("axe")       != std::string::npos) return { 160, 100,  50, 255 };
    if (id.find("pickaxe")   != std::string::npos) return { 130, 130, 150, 255 };
    if (id.find("logs")      != std::string::npos) return { 120,  75,  30, 255 };
    if (id.find("ore")       != std::string::npos ||
        id.find("bar")       != std::string::npos) return { 100, 105, 120, 255 };
    if (id.find("helm")      != std::string::npos ||
        id.find("body")      != std::string::npos ||
        id.find("legs")      != std::string::npos ||
        id.find("shield")    != std::string::npos) return { 170, 175, 185, 255 };
    if (id.find("leather")   != std::string::npos) return { 120,  80,  40, 255 };
    if (id.find("shrimp")    != std::string::npos ||
        id.find("fish")      != std::string::npos) return { 255, 150,  80, 255 };
    if (id.find("coin")      != std::string::npos) return { 255, 210,  40, 255 };
    if (id.find("amulet")    != std::string::npos) return { 200, 140, 200, 255 };
    if (id.find("chaingun")  != std::string::npos) return {  80, 160, 200, 255 };
    return { 160, 160, 160, 255 };
}

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int kCols     = 4;
static constexpr int kRows     = 7;
static constexpr int kSlotSize = 36;
static constexpr int kSlotGap  = 3;
static constexpr int kPadding  = 8;
static constexpr int kTitleH   = 18;

static constexpr int kPanelW =
    kCols * kSlotSize + (kCols-1) * kSlotGap + kPadding * 2;
static constexpr int kPanelH =
    kTitleH + kSlotGap + kRows * kSlotSize + (kRows-1) * kSlotGap + kPadding * 2;

// ── Panel ─────────────────────────────────────────────────────────────────────
void clayInventoryPanel(const shared::PlayerState* player)
{
    static const Clay_String kTitle = CLAY_STRING("Inventory");

    // Floating anchor pinned to bottom-right of screen.
    // Fields in declaration order: offset, expand, parentId, zIndex,
    //   attachPoints, pointerCaptureMode, attachTo, clipTo
    CLAY(CLAY_ID("InventoryAnchor"), {
        .floating = {
            .offset       = { -14.f, -14.f },
            .zIndex       = 10,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                .parent  = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {

        // Outer panel — fields: layout, backgroundColor, ..., cornerRadius, ..., border
        CLAY(CLAY_ID("InventoryPanel"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kPanelW), CLAY_SIZING_FIXED(kPanelH) },
                .padding         = CLAY_PADDING_ALL(kPadding),
                .childGap        = (uint16_t)kSlotGap,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kPanelBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(4),
            .border = {
                .color = kPanelBorder,
                .width = CLAY_BORDER_ALL(1),
            }
        }) {

            // Title row — childAlignment before layoutDirection
            CLAY(CLAY_ID("InvTitle"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kTitleH) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                }
            }) {
                CLAY_TEXT(kTitle, CLAY_TEXT_CONFIG({
                    .textColor = kTitleColor,
                    .fontSize  = 13,
                }));
            }

            // 7 rows × 4 columns
            for (int row = 0; row < kRows; ++row) {
                CLAY(CLAY_IDI("InvRow", row), {
                    .layout = {
                        .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kSlotSize) },
                        .childGap        = (uint16_t)kSlotGap,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    }
                }) {
                    for (int col = 0; col < kCols; ++col) {
                        int idx = row * kCols + col;

                        const shared::ItemStack* item = nullptr;
                        if (player && idx < (int)player->inventory.size()) {
                            const auto& opt = player->inventory[idx];
                            if (opt.has_value()) item = &opt.value();
                        }

                        bool filled = (item != nullptr);

                        // childAlignment before layoutDirection
                        CLAY(CLAY_IDI("InvSlot", idx), {
                            .layout = {
                                .sizing          = { CLAY_SIZING_FIXED(kSlotSize),
                                                     CLAY_SIZING_FIXED(kSlotSize) },
                                .childAlignment  = { .x = CLAY_ALIGN_X_CENTER,
                                                     .y = CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                            },
                            .backgroundColor = filled ? kSlotFilled : kSlotEmpty,
                            .cornerRadius    = CLAY_CORNER_RADIUS(2),
                            .border = {
                                .color = kSlotBorder,
                                .width = CLAY_BORDER_ALL(1),
                            }
                        }) {
                            if (filled) {
                                // Coloured icon placeholder (24×24, centred in 36×36 slot)
                                CLAY(CLAY_IDI("InvIcon", idx), {
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) }
                                    },
                                    .backgroundColor = itemAccent(item->itemId),
                                    .cornerRadius    = CLAY_CORNER_RADIUS(2),
                                }) {}

                                // Quantity text (shown when > 1)
                                if (item->quantity > 1) {
                                    char qtBuf[12];
                                    if (item->quantity >= 1'000'000)
                                        std::snprintf(qtBuf, sizeof(qtBuf), "%dM",
                                                      item->quantity / 1'000'000);
                                    else if (item->quantity >= 1'000)
                                        std::snprintf(qtBuf, sizeof(qtBuf), "%dk",
                                                      item->quantity / 1'000);
                                    else
                                        std::snprintf(qtBuf, sizeof(qtBuf), "%d",
                                                      item->quantity);

                                    // Clay_String: isStaticallyAllocated, length, chars
                                    Clay_String qtStr = {
                                        false,
                                        (int32_t)std::strlen(qtBuf),
                                        qtBuf
                                    };
                                    CLAY_TEXT(qtStr, CLAY_TEXT_CONFIG({
                                        .textColor = kQtyColor,
                                        .fontSize  = 10,
                                    }));
                                }
                            }
                        }
                    }
                }
            }

        } // InventoryPanel
    } // InventoryAnchor
}

} // namespace ui
