// ClayBankPanel.cpp
// OSRS-style bank window rendered entirely with Clay.
// Layout: header (title + usage + X close) + scrollable bank grid (with
// scrollbar) + footer (Deposit All / Deposit Equipment).
// The player's real inventory (HUD panel) is used to deposit — the bank window
// no longer shows a copy of the inventory.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <glad/glad.h>
#include "ui/ClayBankPanel.hpp"
#include "ui/ClayContextMenu.hpp"
#include "ui/NameRegistry.hpp"
#include "net/NetworkClient.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace ui {

// ── Palette ───────────────────────────────────────────────────────────────────
static constexpr Clay_Color kBg         = {  18,  10,   3, 200 };  // alpha matches chat window
static constexpr Clay_Color kBorder     = { 107,  79,  41, 200 };
static constexpr Clay_Color kHeaderBg   = {  10,   6,   2, 255 };
static constexpr Clay_Color kDivider    = {  70,  50,  20, 200 };
static constexpr Clay_Color kSlotEmpty  = {  13,   6,   0, 220 };
static constexpr Clay_Color kSlotFilled = {  30,  16,   4, 240 };
static constexpr Clay_Color kSlotBorder = { 107,  79,  41, 180 };
static constexpr Clay_Color kSlotHover  = { 255, 152,  31, 255 };
static constexpr Clay_Color kQtyText    = { 255, 221,  68, 255 };
static constexpr Clay_Color kBtnBg      = {  35,  20,   7, 255 };
static constexpr Clay_Color kBtnHov     = {  60,  37,  13, 255 };
static constexpr Clay_Color kOrange     = { 255, 152,  31, 255 };
static constexpr Clay_Color kWhite      = { 255, 255, 255, 255 };
static constexpr Clay_Color kGrey       = { 160, 160, 160, 200 };
static constexpr Clay_Color kRed        = { 230,  80,  60, 255 };
static constexpr Clay_Color kScrollBg   = {   8,   4,   0, 180 };
static constexpr Clay_Color kSbTrack    = {  20,  12,   4, 200 };
static constexpr Clay_Color kSbThumb    = {  90,  66,  34, 255 };

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr float kPad         =   8.f;
static constexpr float kCellSize    =  44.f;
static constexpr float kCellGap     =   3.f;
static constexpr int   kCols        =   8;     // bank columns
static constexpr int   kVisibleRows =   6;     // rows visible before scroll
static constexpr int   kMaxBank     = 100;     // BANK_SLOTS (mirror of server)
static constexpr float kSbW         =   8.f;   // scrollbar width
static constexpr float kHeaderH     =  32.f;
static constexpr float kFooterH     =  36.f;

// Grid metrics derived from the constants above.
static constexpr float kGridContentW = kCols * kCellSize + (kCols - 1) * kCellGap;     // 373
static constexpr float kGridRowH     = kVisibleRows * kCellSize
                                     + (kVisibleRows - 1) * kCellGap + 2.f * kPad;       // 295
static constexpr float kPanelW       = kGridContentW + 2.f * kPad + kSbW;               // 397
static constexpr float kPanelH       = kHeaderH + 1.f + kGridRowH + 1.f + kFooterH;     // 365

// ── Per-frame hover state ─────────────────────────────────────────────────────
static int  s_hovBankSlot = -1;   // hovered bank grid slot index

// ── Drag-and-drop state (bank grid reorder) ───────────────────────────────────
static int   s_bkDragSlot   = -1;   // slot being dragged (-1 = none)
static int   s_bkPressSlot  = -1;   // slot pressed but not yet dragged
static float s_bkPressX     =  0.f;
static float s_bkPressY     =  0.f;
static bool  s_bkPrevDown   = false;

// ── Close flag ────────────────────────────────────────────────────────────────
static bool s_wantsClose = false;

bool bankWantsClose() {
    bool v = s_wantsClose;
    s_wantsClose = false;
    return v;
}

// ── String scratch ────────────────────────────────────────────────────────────
static char s_buf[4096];
static int  s_boff = 0;

static Clay_String cs(const char* src) {
    int len = static_cast<int>(std::strlen(src));
    if (s_boff + len + 1 > static_cast<int>(sizeof(s_buf)))
        len = static_cast<int>(sizeof(s_buf)) - s_boff - 1;
    if (len <= 0) return { false, 0, "" };
    char* dst = s_buf + s_boff;
    std::memcpy(dst, src, len);
    dst[len] = '\0';
    s_boff += len + 1;
    return { false, len, dst };
}

static Clay_String csStr(const std::string& s) { return cs(s.c_str()); }

static std::string fmtQty(int q) {
    char tmp[16];
    if      (q >= 10000000) std::snprintf(tmp, sizeof(tmp), "%dM",   q / 1000000);
    else if (q >=  1000000) std::snprintf(tmp, sizeof(tmp), "%.1fM", q / 1000000.0f);
    else if (q >=    10000) std::snprintf(tmp, sizeof(tmp), "%dk",   q / 1000);
    else if (q >=     1000) std::snprintf(tmp, sizeof(tmp), "%.1fk", q / 1000.0f);
    else                    std::snprintf(tmp, sizeof(tmp), "%d",    q);
    return tmp;
}

// ── Single bank slot element ──────────────────────────────────────────────────
static void buildBankSlot(int idx, const shared::ItemStack* item,
                          const SpriteCache* sprites) {
    bool hovered = (s_hovBankSlot == idx);

    CLAY(CLAY_IDI("BkBankSlot", idx), {
        .layout = {
            .sizing         = { CLAY_SIZING_FIXED(kCellSize), CLAY_SIZING_FIXED(kCellSize) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = item ? kSlotFilled : kSlotEmpty,
        .border = {
            .color = hovered ? kSlotHover : kSlotBorder,
            .width = CLAY_BORDER_ALL(1),
        }
    }) {
        if (item) {
            if (sprites) {
                GLuint tex = sprites->get(item->itemId);
                CLAY(CLAY_IDI("BkBankSprite", idx), {
                    .layout = {
                        // 32×32 sprites drawn 1:1 (crisp, no scaling/filtering).
                        .sizing = { CLAY_SIZING_FIXED(32.f), CLAY_SIZING_FIXED(32.f) },
                    },
                    .image = {
                        .imageData = reinterpret_cast<void*>(static_cast<uintptr_t>(tex))
                    }
                }) {}
            }

            // Quantity overlay (top-RIGHT floating) — items always stack in bank.
            if (item->quantity > 1) {
                std::string qs = fmtQty(item->quantity);
                CLAY(CLAY_IDI("BkBankQty", idx), {
                    .floating = {
                        .offset       = { -2.f, 2.f },
                        .zIndex       = 10,
                        .attachPoints = {
                            .element = CLAY_ATTACH_POINT_RIGHT_TOP,
                            .parent  = CLAY_ATTACH_POINT_RIGHT_TOP,
                        },
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                    }
                }) {
                    CLAY_TEXT(csStr(qs), CLAY_TEXT_CONFIG({
                        .textColor = kQtyText,
                        .fontSize  = 0,
                    }));
                }
            }
        }
    }
}

// ── Public: build layout ──────────────────────────────────────────────────────
void buildBankPanel(float /*screenW*/, float /*screenH*/,
                    const shared::PlayerState* player,
                    net::NetworkClient* netc,
                    const SpriteCache* sprites,
                    bool bankOpen,
                    bool leftClicked,
                    bool rightClicked,
                    bool mouseDown,
                    float mx, float my,
                    UiHoverState* hover) {
    if (!bankOpen) {
        s_bkDragSlot = s_bkPressSlot = -1;
        s_bkPrevDown = false;
        return;
    }

    s_boff = 0;

    // Total bank slots (padded array, e.g. 100) and the filled/last-filled count.
    int bankLen = player ? static_cast<int>(player->bank.size()) : 0;
    int filled = 0, lastFilled = -1;
    for (int i = 0; i < bankLen; ++i)
        if (player->bank[i].has_value()) { ++filled; lastFilled = i; }

    // Render enough rows to cover all items plus one spare row for drop targets,
    // so the window stays compact instead of showing all 100 empty slots.
    const int totalRows  = (bankLen + kCols - 1) / kCols;
    const int usedRows   = (lastFilled + kCols) / kCols;   // ceil((lastFilled+1)/kCols)
    const int rows       = std::max(1, std::min(usedRows + 1, std::max(1, totalRows)));
    const int renderSlots = std::min(bankLen, rows * kCols);

    // ── Recompute hover from last frame's bounding boxes ─────────────────────
    s_hovBankSlot = -1;
    for (int i = 0; i < renderSlots; ++i)
        if (Clay_PointerOver(CLAY_IDI("BkBankSlot", i))) { s_hovBankSlot = i; break; }

    // ── Button hover detection ────────────────────────────────────────────────
    bool dAllHov  = Clay_PointerOver(CLAY_ID("BkDepositAll"));
    bool dWornHov = Clay_PointerOver(CLAY_ID("BkDepositWorn"));
    bool xHov     = Clay_PointerOver(CLAY_ID("BkClose"));

    // ── Hover tooltip for a bank item: "Withdraw-1 {Item}" ───────────────────
    if (hover && s_hovBankSlot >= 0 && s_hovBankSlot < bankLen && s_bkDragSlot < 0) {
        const auto& opt = player->bank[s_hovBankSlot];
        if (opt.has_value()) {
            hover->kind     = UiHoverState::Kind::InventoryItem;
            hover->verb     = "Withdraw-1";
            hover->itemName = ui::itemName(opt->itemId);
        }
    }

    // ── Close handling: X button, or click outside both bank + HUD panels.
    // Never close while a context menu is up (so clicking a Deposit/Withdraw
    // entry doesn't also dismiss the bank).
    if (leftClicked) {
        if (xHov) {
            s_wantsClose = true;
        } else if (!Clay_PointerOver(CLAY_ID("BkPanel")) &&
                   !Clay_PointerOver(CLAY_ID("HudPanel")) &&
                   !ctxMenu().open) {
            s_wantsClose = true;
        }
    }

    // ── Footer button actions ─────────────────────────────────────────────────
    if (leftClicked) {
        if (dAllHov  && netc) netc->sendDepositAll();
        if (dWornHov && netc) netc->sendDepositWorn();
    }

    // ── Right-click bank grid slot → Withdraw 1/5/10/All/Examine ─────────────
    if (rightClicked && s_hovBankSlot >= 0 && s_hovBankSlot < bankLen) {
        s_bkDragSlot = s_bkPressSlot = -1;   // cancel any drag
        const auto& opt = player->bank[s_hovBankSlot];
        if (opt.has_value()) {
            auto& cm = ctxMenu();
            cm.open             = true;
            cm.bankGridCtxSlot  = s_hovBankSlot;
            cm.bankInvCtxSlot   = -1;
            cm.inventoryCtxSlot = -1;
            cm.equipCtxSlot.clear();
            cm.contextItemId    = opt->itemId;
            ImVec2 mp = ImGui::GetIO().MousePos;
            cm.x = mp.x; cm.y = mp.y;
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            cm.screenW = ds.x; cm.screenH = ds.y;
            cm.entries.clear(); cm.clickedIndex = -1;
            std::string name = ui::itemName(opt->itemId);
            cm.entries.push_back({ "Withdraw 1",   name });
            cm.entries.push_back({ "Withdraw 5",   name });
            cm.entries.push_back({ "Withdraw 10",  name });
            cm.entries.push_back({ "Withdraw All", name });
            cm.entries.push_back({ "Examine",      name });
        }
    }

    // ── Drag-to-reorder + left-click withdraw-1 ──────────────────────────────
    // Mirrors the inventory: press → (move>threshold) drag → drop swaps slots;
    // a plain click (no drag) withdraws 1 of the item to the inventory.
    {
        const bool wasDown  = s_bkPrevDown;
        s_bkPrevDown        = mouseDown;
        const bool downEdge = (mouseDown && !wasDown);
        const bool upEdge   = (!mouseDown && wasDown);

        // Press on a filled slot records it.
        if (downEdge && s_hovBankSlot >= 0 && s_hovBankSlot < bankLen &&
            player->bank[s_hovBankSlot].has_value()) {
            s_bkPressSlot = s_hovBankSlot;
            s_bkPressX = mx; s_bkPressY = my;
        }

        // Promote to drag once the cursor moves beyond a small threshold.
        constexpr float kDragThresh = 5.f;
        if (mouseDown && s_bkPressSlot >= 0 && s_bkDragSlot < 0) {
            float dx = mx - s_bkPressX, dy = my - s_bkPressY;
            if (dx*dx + dy*dy > kDragThresh*kDragThresh) s_bkDragSlot = s_bkPressSlot;
        }

        if (upEdge) {
            if (s_bkDragSlot >= 0) {
                // Drop: swap onto the hovered slot (if different).
                if (s_hovBankSlot >= 0 && s_hovBankSlot != s_bkDragSlot && netc)
                    netc->sendMoveBankSlot(s_bkDragSlot, s_hovBankSlot);
                s_bkDragSlot = -1;
            } else if (s_bkPressSlot >= 0 && s_bkPressSlot == s_hovBankSlot &&
                       !ctxMenu().open && netc) {
                // Plain click → withdraw 1.
                netc->sendWithdrawItem(s_bkPressSlot, 1);
            }
            s_bkPressSlot = -1;
        }
    }

    // ── Scrollbar thumb geometry (from previous-frame scroll data) ───────────
    Clay_ScrollContainerData sd =
        Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("BkGridScroll")));
    const float kTrackH = kGridRowH;
    float thumbH = kTrackH, thumbOffsetY = 0.f;
    bool  showSb = false;
    if (sd.found && sd.contentDimensions.height > sd.scrollContainerDimensions.height) {
        showSb         = true;
        float viewH    = sd.scrollContainerDimensions.height;
        float contentH = sd.contentDimensions.height;
        thumbH         = std::max(16.f, (viewH / contentH) * kTrackH);
        float maxOff   = kTrackH - thumbH;
        float frac     = (-sd.scrollPosition->y) / (contentH - viewH);
        frac           = std::max(0.f, std::min(1.f, frac));
        thumbOffsetY   = frac * maxOff;
    }

    // ── Layout — centred on screen ────────────────────────────────────────────
    CLAY(CLAY_ID("BkAnchor"), {
        .floating = {
            .offset   = { 0.f, 0.f },
            .zIndex   = 20,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent  = CLAY_ATTACH_POINT_CENTER_CENTER,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        // Drag ghost — the dragged item's sprite follows the cursor.
        if (s_bkDragSlot >= 0 && s_bkDragSlot < bankLen &&
            player->bank[s_bkDragSlot].has_value() && sprites) {
            GLuint tex = sprites->get(player->bank[s_bkDragSlot]->itemId);
            CLAY(CLAY_ID("BkDragGhost"), {
                .floating = {
                    .offset  = { mx - 16.f, my - 16.f },
                    .zIndex  = 60,
                    .attachPoints = {
                        .element = CLAY_ATTACH_POINT_LEFT_TOP,
                        .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
                    },
                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                    .attachTo = CLAY_ATTACH_TO_ROOT,
                }
            }) {
                CLAY(CLAY_ID("BkDragGhostSprite"), {
                    .layout = { .sizing = { CLAY_SIZING_FIXED(32.f), CLAY_SIZING_FIXED(32.f) } },
                    .image  = { .imageData = reinterpret_cast<void*>(static_cast<uintptr_t>(tex)) }
                }) {}
            }
        }

        CLAY(CLAY_ID("BkPanel"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kPanelW), CLAY_SIZING_FIXED(kPanelH) },
                .childGap        = 0,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(4),
            .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) }
        }) {
            // ── Header: "Bank"  …  "12 / 400"  [X] ───────────────────────────
            CLAY(CLAY_ID("BkHeader"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kHeaderH) },
                    .padding         = { (uint16_t)kPad, (uint16_t)kPad, 6, 6 },
                    .childGap        = 8,
                    .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
                .backgroundColor = kHeaderBg,
            }) {
                CLAY_TEXT(CLAY_STRING("Bank"), CLAY_TEXT_CONFIG({
                    .textColor = kOrange, .fontSize = 0,
                }));

                // Spacer
                CLAY(CLAY_ID("BkHeaderSpacer"), {
                    .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }
                }) {}

                // Usage fraction — number of filled slots out of the bank max.
                {
                    char usageBuf[32];
                    std::snprintf(usageBuf, sizeof(usageBuf), "%d / %d", filled, kMaxBank);
                    CLAY_TEXT(cs(usageBuf), CLAY_TEXT_CONFIG({
                        .textColor = kGrey, .fontSize = 0,
                    }));
                }

                // Close (X) button
                CLAY(CLAY_ID("BkClose"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = xHov ? kBtnHov : kBtnBg,
                    .cornerRadius    = CLAY_CORNER_RADIUS(2),
                    .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) }
                }) {
                    CLAY_TEXT(CLAY_STRING("X"), CLAY_TEXT_CONFIG({
                        .textColor = kRed, .fontSize = 0,
                    }));
                }
            }

            // ── Header/content divider ────────────────────────────────────────
            CLAY(CLAY_ID("BkTopDivider"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                .backgroundColor = kDivider,
            }) {}

            // ── Bank grid row: scrollable grid + scrollbar track ──────────────
            CLAY(CLAY_ID("BkGridRow"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kGridRowH) },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
            }) {
                // Scrollable grid (padded on each side)
                CLAY(CLAY_ID("BkGridScroll"), {
                    .layout = {
                        .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                        .padding         = { (uint16_t)kPad, (uint16_t)kPad,
                                             (uint16_t)kPad, (uint16_t)kPad },
                        .childGap        = (uint16_t)kCellGap,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    },
                    .backgroundColor = kScrollBg,
                    .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
                }) {
                    for (int row = 0; row < rows; ++row) {
                        CLAY(CLAY_IDI("BkBankRow", row), {
                            .layout = {
                                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kCellSize) },
                                .childGap        = (uint16_t)kCellGap,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                            }
                        }) {
                            for (int col = 0; col < kCols; ++col) {
                                int idx = row * kCols + col;
                                const shared::ItemStack* item = nullptr;
                                if (player && idx < bankLen && idx != s_bkDragSlot) {
                                    const auto& opt = player->bank[idx];
                                    if (opt.has_value()) item = &opt.value();
                                }
                                buildBankSlot(idx, item, sprites);
                            }
                        }
                    }
                }

                // Scrollbar track + thumb
                CLAY(CLAY_ID("BkSbTrack"), {
                    .layout = {
                        .sizing          = { CLAY_SIZING_FIXED(kSbW), CLAY_SIZING_FIXED(kGridRowH) },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    },
                    .backgroundColor = kSbTrack,
                }) {
                    if (showSb) {
                        if (thumbOffsetY > 0.f) {
                            CLAY(CLAY_ID("BkSbBefore"), {
                                .layout = { .sizing = { CLAY_SIZING_FIXED(kSbW),
                                                        CLAY_SIZING_FIXED(thumbOffsetY) } },
                            }) {}
                        }
                        CLAY(CLAY_ID("BkSbThumb"), {
                            .layout = { .sizing = { CLAY_SIZING_FIXED(kSbW),
                                                    CLAY_SIZING_FIXED(thumbH) } },
                            .backgroundColor = kSbThumb,
                            .cornerRadius    = CLAY_CORNER_RADIUS(2),
                        }) {}
                    }
                }
            }

            // ── Footer divider ────────────────────────────────────────────────
            CLAY(CLAY_ID("BkFooterDivider"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                .backgroundColor = kDivider,
            }) {}

            // ── Footer: Deposit All / Deposit Equipment ───────────────────────
            CLAY(CLAY_ID("BkFooter"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kFooterH) },
                    .padding         = { (uint16_t)kPad, (uint16_t)kPad, 6, 6 },
                    .childGap        = 6,
                    .childAlignment  = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
                .backgroundColor = kHeaderBg,
            }) {
                CLAY(CLAY_ID("BkDepositAll"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(22) },
                        .padding        = { 8, 8, 4, 4 },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = dAllHov ? kBtnHov : kBtnBg,
                    .cornerRadius    = CLAY_CORNER_RADIUS(2),
                    .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) }
                }) {
                    CLAY_TEXT(CLAY_STRING("Deposit All"), CLAY_TEXT_CONFIG({
                        .textColor = kWhite, .fontSize = 0,
                    }));
                }

                CLAY(CLAY_ID("BkDepositWorn"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(22) },
                        .padding        = { 8, 8, 4, 4 },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = dWornHov ? kBtnHov : kBtnBg,
                    .cornerRadius    = CLAY_CORNER_RADIUS(2),
                    .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) }
                }) {
                    CLAY_TEXT(CLAY_STRING("Deposit Equipment"), CLAY_TEXT_CONFIG({
                        .textColor = kWhite, .fontSize = 0,
                    }));
                }
            }
        }
    }
}

} // namespace ui
