// ClayBankPanel.cpp
// OSRS-style bank window rendered entirely with Clay.
// Layout: header (Deposit All / Deposit Equipment / usage) + scrollable bank grid
// + divider + inventory grid (8-col view). Right-click uses ClayContextMenu.

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
static constexpr Clay_Color kBg         = {  18,  10,   3, 240 };
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
static constexpr Clay_Color kScrollBg   = {   8,   4,   0, 180 };

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr float kPanelW    = 460.f;
static constexpr float kPanelH    = 440.f;
static constexpr float kPad       =   8.f;
static constexpr float kCellSize  =  44.f;
static constexpr float kCellGap   =   3.f;
static constexpr int   kCols      =   8;   // 8 columns in bank / inventory-in-bank
static constexpr int   kMaxBank   = 400;

// Inventory-in-bank: show up to 2 rows × 8 cols = 16 visible at once.
static constexpr int kInvBankRows = 2;

// ── Per-frame hover state ─────────────────────────────────────────────────────
static int  s_hovBankSlot = -1;   // hovered bank grid slot index
static int  s_hovInvSlot  = -1;   // hovered inventory slot index (in bank view)

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

// ── Single item slot element (shared by bank grid and inventory grid) ─────────
// Suffix 'B' for bank grid slots, 'V' for inventory-in-bank slots.
static void buildBankSlot(int idx, bool isBankGrid,
                          const shared::ItemStack* item,
                          const SpriteCache* sprites) {
    bool hovered = isBankGrid ? (s_hovBankSlot == idx) : (s_hovInvSlot == idx);

    Clay_ElementId elemId = isBankGrid ? CLAY_IDI("BkBankSlot", idx)
                                       : CLAY_IDI("BkInvSlot",  idx);
    CLAY(elemId, {
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
                Clay_ElementId sprId = isBankGrid ? CLAY_IDI("BkBankSprite", idx)
                                                  : CLAY_IDI("BkInvSprite",  idx);
                CLAY(sprId, {
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(kCellSize - 6),
                                    CLAY_SIZING_FIXED(kCellSize - 6) },
                    },
                    .image = {
                        .imageData = reinterpret_cast<void*>(static_cast<uintptr_t>(tex))
                    }
                }) {}
            }

            // Quantity overlay (top-left floating)
            if (item->quantity > 1) {
                std::string qs = fmtQty(item->quantity);
                Clay_ElementId qtyId = isBankGrid ? CLAY_IDI("BkBankQty", idx)
                                                  : CLAY_IDI("BkInvQty",  idx);
                CLAY(qtyId, {
                    .floating = {
                        .offset       = { 2.f, 2.f },
                        .attachPoints = {
                            .element = CLAY_ATTACH_POINT_LEFT_TOP,
                            .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
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
                    bool rightClicked) {
    if (!bankOpen) return;

    s_boff = 0;

    // ── Recompute hover from last frame's bounding boxes ─────────────────────
    s_hovBankSlot = -1;
    s_hovInvSlot  = -1;
    {
        int bankCount = player ? static_cast<int>(player->bank.size()) : 0;
        for (int i = 0; i < bankCount; ++i)
            if (Clay_PointerOver(CLAY_IDI("BkBankSlot", i))) { s_hovBankSlot = i; break; }

        int invCount = player ? static_cast<int>(player->inventory.size()) : 0;
        for (int i = 0; i < invCount && i < kCols * kInvBankRows; ++i)
            if (Clay_PointerOver(CLAY_IDI("BkInvSlot", i))) { s_hovInvSlot = i; break; }
    }

    // ── Button hover detection ────────────────────────────────────────────────
    bool dAllHov  = Clay_PointerOver(CLAY_ID("BkDepositAll"));
    bool dWornHov = Clay_PointerOver(CLAY_ID("BkDepositWorn"));

    // ── Close on outside click ────────────────────────────────────────────────
    if (leftClicked && !Clay_PointerOver(CLAY_ID("BkPanel"))) {
        s_wantsClose = true;
    }

    // ── Header button actions ─────────────────────────────────────────────────
    if (leftClicked) {
        if (dAllHov  && netc) netc->sendDepositAll();
        if (dWornHov && netc) netc->sendDepositWorn();
    }

    // ── Right-click bank grid slot ────────────────────────────────────────────
    if (rightClicked && s_hovBankSlot >= 0 && player &&
        s_hovBankSlot < static_cast<int>(player->bank.size())) {
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
            cm.entries.push_back({ "Withdraw All", name });
            cm.entries.push_back({ "Examine",      name });
        }
    }

    // ── Right-click inventory slot in bank ────────────────────────────────────
    if (rightClicked && s_hovInvSlot >= 0 && player &&
        s_hovInvSlot < static_cast<int>(player->inventory.size())) {
        const auto& opt = player->inventory[s_hovInvSlot];
        if (opt.has_value()) {
            auto& cm = ctxMenu();
            cm.open             = true;
            cm.bankInvCtxSlot   = s_hovInvSlot;
            cm.bankGridCtxSlot  = -1;
            cm.inventoryCtxSlot = -1;
            cm.equipCtxSlot.clear();
            cm.contextItemId    = opt->itemId;
            ImVec2 mp = ImGui::GetIO().MousePos;
            cm.x = mp.x; cm.y = mp.y;
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            cm.screenW = ds.x; cm.screenH = ds.y;
            cm.entries.clear(); cm.clickedIndex = -1;
            std::string name = ui::itemName(opt->itemId);
            cm.entries.push_back({ "Deposit 1",   name });
            cm.entries.push_back({ "Deposit All", name });
        }
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    // Centred on screen via CLAY_ATTACH_POINT_CENTER_CENTER
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
        // Main panel box
        CLAY(CLAY_ID("BkPanel"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kPanelW), CLAY_SIZING_FIXED(kPanelH) },
                .childGap        = 0,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(4),
            .border = {
                .color = kBorder,
                .width = CLAY_BORDER_ALL(1),
            }
        }) {
            // ── Header ───────────────────────────────────────────────────────
            CLAY(CLAY_ID("BkHeader"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) },
                    .padding         = { (uint16_t)kPad, (uint16_t)kPad, 8, 8 },
                    .childGap        = 6,
                    .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
                .backgroundColor = kHeaderBg,
            }) {
                // Deposit All button
                CLAY(CLAY_ID("BkDepositAll"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(22) },
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

                // Deposit Equipment button
                CLAY(CLAY_ID("BkDepositWorn"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(22) },
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

                // Spacer to push usage text right
                CLAY(CLAY_ID("BkHeaderSpacer"), {
                    .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }
                }) {}

                // Usage fraction (e.g. "12 / 400")
                {
                    int used = player ? static_cast<int>(player->bank.size()) : 0;
                    char usageBuf[32];
                    std::snprintf(usageBuf, sizeof(usageBuf), "%d / %d", used, kMaxBank);
                    CLAY_TEXT(cs(usageBuf), CLAY_TEXT_CONFIG({
                        .textColor = kGrey, .fontSize = 0,
                    }));
                }
            }

            // ── Header/content divider ────────────────────────────────────────
            CLAY(CLAY_ID("BkTopDivider"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                .backgroundColor = kDivider,
            }) {}

            // ── Bank grid section label ───────────────────────────────────────
            CLAY(CLAY_ID("BkGridLabel"), {
                .layout = {
                    .sizing   = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20) },
                    .padding  = { (uint16_t)kPad, (uint16_t)kPad, 4, 4 },
                    .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = kHeaderBg,
            }) {
                CLAY_TEXT(CLAY_STRING("Bank"), CLAY_TEXT_CONFIG({
                    .textColor = kOrange, .fontSize = 0,
                }));
            }

            // ── Scrollable bank grid ──────────────────────────────────────────
            // Available height = panel - header(36) - divider(1) - label(20)
            //                  - mid-divider(1) - inv-label(20) - inv-grid(~2*44+3+2*8=107)
            // We allocate the remaining space for the bank grid.
            static constexpr float kInvSectionH = 1.f + 20.f + 2 * kCellSize + kCellGap
                                                   + 2.f * kPad + 1.f;
            static constexpr float kBankGridH   = kPanelH - 36.f - 1.f - 20.f - kInvSectionH;

            CLAY(CLAY_ID("BkGridScroll"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kBankGridH) },
                    .padding         = { (uint16_t)kPad, (uint16_t)kPad,
                                         (uint16_t)kPad, (uint16_t)kPad },
                    .childGap        = (uint16_t)kCellGap,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                },
                .backgroundColor = kScrollBg,
                .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
            }) {
                int bankCount = player ? static_cast<int>(player->bank.size()) : 0;
                // Pad to full row so trailing slots are visible
                int rows = std::max(1, (bankCount + kCols - 1) / kCols);
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
                            if (player && idx < bankCount) {
                                const auto& opt = player->bank[idx];
                                if (opt.has_value()) item = &opt.value();
                            }
                            buildBankSlot(idx, /*isBankGrid=*/true, item, sprites);
                        }
                    }
                }
            }

            // ── Mid divider ───────────────────────────────────────────────────
            CLAY(CLAY_ID("BkMidDivider"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                .backgroundColor = kDivider,
            }) {}

            // ── Inventory section label ───────────────────────────────────────
            CLAY(CLAY_ID("BkInvLabel"), {
                .layout = {
                    .sizing   = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20) },
                    .padding  = { (uint16_t)kPad, (uint16_t)kPad, 4, 4 },
                    .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = kHeaderBg,
            }) {
                CLAY_TEXT(CLAY_STRING("Inventory  (right-click to deposit)"),
                    CLAY_TEXT_CONFIG({ .textColor = kGrey, .fontSize = 0 }));
            }

            // ── Inventory grid (2 rows × 8 cols) ──────────────────────────────
            CLAY(CLAY_ID("BkInvGrid"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .padding         = { (uint16_t)kPad, (uint16_t)kPad,
                                         (uint16_t)kPad, (uint16_t)kPad },
                    .childGap        = (uint16_t)kCellGap,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                },
                .backgroundColor = kScrollBg,
            }) {
                int invCount = player ? static_cast<int>(player->inventory.size()) : 0;
                for (int row = 0; row < kInvBankRows; ++row) {
                    CLAY(CLAY_IDI("BkInvRow", row), {
                        .layout = {
                            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kCellSize) },
                            .childGap        = (uint16_t)kCellGap,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        }
                    }) {
                        for (int col = 0; col < kCols; ++col) {
                            int idx = row * kCols + col;
                            const shared::ItemStack* item = nullptr;
                            if (player && idx < invCount) {
                                const auto& opt = player->inventory[idx];
                                if (opt.has_value()) item = &opt.value();
                            }
                            buildBankSlot(idx, /*isBankGrid=*/false, item, sprites);
                        }
                    }
                }
            }
        }
    }
}

} // namespace ui
